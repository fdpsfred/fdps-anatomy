// Export everything a global-data agent needs about one global symbol, read-only.
//
// Ticket 17 gives every global data symbol a semantic name and a real type, so
// that the rebuilt C reads as field accesses instead of offset arithmetic. The
// judgement is made from the referencing code, so each agent needs the bytes at
// the address AND every instruction that touches it, with the function it sits
// in. Fetching that through the Ghidra MCP once per agent would serialise a
// thousand agents behind one database; dumping it once lets them run in
// parallel off files.
//
// Nothing here is a judgement. The dump states what the program contains and
// what Ghidra currently claims. In particular the span it reports for each
// anchor -- anchor to the next referenced or defined address -- is a mechanical
// measurement, not a claim that those bytes are one variable. Deciding that is
// the agent's job, and ApplyGlobalVerdicts.java writes the answer back.
//
// Usage: run_ghidra_script with one argument:
//   <output dir>        writes globals.json and ev/ under it
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.AbstractStringDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class DumpGlobalState extends GhidraScript {

	/** Bytes of a span reproduced in an evidence file. */
	private static final int MAX_BYTES = 512;
	/** Instructions of context either side of a referencing instruction. */
	private static final int CONTEXT_INSNS = 4;
	/** Decompiled lines quoted per referencing function. */
	private static final int MAX_DEC_LINES = 24;
	/** Longest string literal reproduced. */
	private static final int MAX_STRING = 200;

	// The DGROUP segment boundaries, from program_info/memory_layout.md. They
	// come out of the CRT startup code loading _edata and _end as immediates,
	// not out of anything Ghidra inferred, which is why they are constants here
	// rather than something this script tries to rediscover every run.
	private static final long DATA_END = 0x63930L;   // _edata
	private static final long BSS_END = 0x6a3bcL;    // _end
	private static final long STACK_START = 0x6a3c0L;
	/** object 2 is file-backed only this far; above it the loader zeroes. */
	private static final long OBJ2_FILE_END = 0x64000L;

	private Memory mem;
	private MemoryBlock codeBlock;
	private DecompInterface decomp;
	private final Map<String, String[]> decompCache = new HashMap<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: DumpGlobalState <output dir>");
			return;
		}
		File outDir = new File(argv[0]);
		File evDir = new File(outDir, "ev");
		evDir.mkdirs();

		mem = currentProgram.getMemory();
		codeBlock = mem.getBlock(".object1");

		decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		// ---- collect anchors -------------------------------------------------
		// An anchor is an address some instruction reads or writes as data, plus
		// every defined data item in the writable blocks, so that a table Ghidra
		// already knows about does not vanish because only its middle is
		// referenced.
		Map<Address, List<Reference>> refs = new TreeMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext()) {
			Instruction ins = it.next();
			for (Reference r : ins.getReferencesFrom()) {
				Address to = r.getToAddress();
				if (to == null || !inScope(to)) {
					continue;
				}
				RefType t = r.getReferenceType();
				if (t.isCall() || t.isJump() || t.isFlow()) {
					continue;
				}
				refs.computeIfAbsent(to, k -> new ArrayList<>()).add(r);
			}
		}
		Set<Address> anchors = new TreeSet<>(refs.keySet());
		for (String name : new String[] { ".object2", ".object3" }) {
			MemoryBlock b = mem.getBlock(name);
			if (b == null) {
				continue;
			}
			for (Data d : currentProgram.getListing().getDefinedData(
					new AddressSet(b.getStart(), b.getEnd()), true)) {
				anchors.add(d.getAddress());
			}
		}
		// Owners that no instruction names. An anchor is where a reference
		// lands, so an object whose base is never referenced -- because the
		// compiler folded it into a displacement and every use reaches a field
		// instead -- is invisible to the sweep above, and its fields end up as
		// interiors pointing at an address with no verdict of its own. The
		// agents that judged those fields said where the base is; this reads
		// those back so the base gets judged too. It carries no judgement of its
		// own: the address came from a verdict, and build_worklist.py writes the
		// file.
		int extra = 0;
		File extraFile = new File(outDir.getParentFile(), "extra_anchors.txt");
		if (extraFile.isFile()) {
			for (String line : java.nio.file.Files.readAllLines(extraFile.toPath())) {
				String s = line.trim();
				if (s.isEmpty() || s.startsWith("#")) {
					continue;
				}
				try {
					Address a = toAddr(s);
					if (a != null && inScope(a) && anchors.add(a)) {
						extra++;
					}
				}
				catch (Exception e) {
					println("WARN: extra anchor " + s + " is not usable: " + e.getMessage());
				}
			}
			println("extra anchors from verdicts = " + extra);
		}

		List<Address> ordered = new ArrayList<>(anchors);
		println("anchors = " + ordered.size());

		// ---- write one evidence file per anchor ------------------------------
		JsonArray index = new JsonArray();
		int strings = 0;
		for (int i = 0; i < ordered.size(); i++) {
			Address a = ordered.get(i);
			Address next = i + 1 < ordered.size() ? ordered.get(i + 1) : null;
			Address prev = i > 0 ? ordered.get(i - 1) : null;
			JsonObject entry = describe(a, next, prev, refs.get(a));
			if (entry.get("is_string").getAsBoolean()) {
				strings++;
			}
			index.add(summarise(entry));
			writeJson(new File(evDir, key(a) + ".json"), entry);
			if (i % 200 == 0) {
				println("  " + i + "/" + ordered.size());
			}
		}

		JsonObject top = new JsonObject();
		top.add("globals", index);
		top.addProperty("count", index.size());
		top.addProperty("strings", strings);
		writeJson(new File(outDir, "globals.json"), top);
		println("wrote " + index.size() + " evidence files, " + strings + " of them string literals");
		decomp.dispose();
	}

	/** In scope: the writable blocks, and read-only data outside any function body. */
	private boolean inScope(Address a) {
		if (a.getAddressSpace().isOverlaySpace()) {
			return false;
		}
		MemoryBlock b = mem.getBlock(a);
		if (b == null) {
			return false;
		}
		if (b.isWrite()) {
			return true;
		}
		if (b.equals(codeBlock)) {
			// A const table among the code. Anything inside a function body is a
			// jump table, which belongs to that function and not to this ticket.
			return getFunctionContaining(a) == null;
		}
		return false;
	}

	private JsonObject describe(Address a, Address next, Address prev, List<Reference> incoming) {
		JsonObject o = new JsonObject();
		o.addProperty("addr", key(a));
		MemoryBlock b = mem.getBlock(a);
		o.addProperty("block", b == null ? "?" : b.getName());
		o.addProperty("segment", segmentOf(a));

		Symbol s = getSymbolAt(a);
		o.addProperty("symbol", s == null ? "" : s.getName());
		o.addProperty("symbol_source", s == null ? "none" : s.getSource().toString());

		Data d = getDataAt(a);
		Data containing = getDataContaining(a);
		o.addProperty("type", d == null ? "" : d.getDataType().getPathName());
		o.addProperty("defined_size", d == null ? 0 : d.getLength());
		boolean isString = d != null && d.getDataType() instanceof AbstractStringDataType;
		o.addProperty("is_string", isString);
		if (isString) {
			Object v = d.getValue();
			String txt = v == null ? "" : v.toString();
			if (txt.length() > MAX_STRING) {
				txt = txt.substring(0, MAX_STRING) + "...";
			}
			o.addProperty("string_value", txt);
		}
		if (containing != null && !containing.getAddress().equals(a)) {
			o.addProperty("inside_defined_data", key(containing.getAddress()));
			o.addProperty("inside_offset", a.subtract(containing.getAddress()));
		}

		// The mechanical span: anchor to the next anchor, or the end of the
		// block. It says where reference density stops, not where the variable
		// stops -- that is the agent's call.
		long spanEnd;
		if (next != null && next.getAddressSpace().equals(a.getAddressSpace())
				&& next.getOffset() > a.getOffset()) {
			spanEnd = next.getOffset();
		}
		else {
			spanEnd = b == null ? a.getOffset() + 1 : b.getEnd().getOffset() + 1;
		}
		long span = spanEnd - a.getOffset();
		o.addProperty("span_to_next_anchor", span);
		o.addProperty("next_anchor", next == null ? "" : key(next));
		o.addProperty("prev_anchor", prev == null ? "" : key(prev));

		// Bytes. In initialised memory these are the initial value; in _BSS they
		// are zero by definition and saying so beats a wall of 00.
		boolean init = isInitialised(a);
		o.addProperty("initialised", init);
		if (init) {
			int len = (int) Math.min(span, MAX_BYTES);
			o.addProperty("bytes", hexdump(a, len));
			o.addProperty("bytes_truncated", span > MAX_BYTES);
		}

		// Every instruction that touches it, with its function and neighbourhood.
		JsonArray xrefs = new JsonArray();
		Set<String> funcs = new TreeSet<>();
		if (incoming != null) {
			for (Reference r : incoming) {
				JsonObject x = new JsonObject();
				Address from = r.getFromAddress();
				x.addProperty("from", key(from));
				x.addProperty("ref_type", r.getReferenceType().getName());
				Function f = getFunctionContaining(from);
				String fname = f == null ? "" : f.getName();
				x.addProperty("function", fname);
				if (f != null) {
					funcs.add(fname + "@" + key(f.getEntryPoint()) + "#" + poolOf(f));
				}
				x.addProperty("context", insnContext(from));
				xrefs.add(x);
			}
		}
		o.add("xrefs", xrefs);
		o.addProperty("xref_count", xrefs.size());

		JsonArray fs = new JsonArray();
		for (String f : funcs) {
			fs.add(f);
		}
		o.add("referencing_functions", fs);
		// Which pools reach this address. A global touched only by AIL functions
		// is AIL's; one touched by both a game function and the CRT is usually a
		// CRT variable the game pokes, and the agent has to say which.
		Set<String> pools = new TreeSet<>();
		for (String f : funcs) {
			pools.add(f.substring(f.lastIndexOf('#') + 1));
		}
		JsonArray ps = new JsonArray();
		for (String p : pools) {
			ps.add(p);
		}
		o.add("referencing_pools", ps);

		// The decompiled lines that mention it, which is where the offset
		// arithmetic this ticket exists to remove is visible.
		JsonArray dec = new JsonArray();
		String needle = s == null ? null : s.getName();
		if (needle != null && !needle.isEmpty()) {
			for (String f : funcs) {
				String entry = f.substring(f.indexOf('@') + 1, f.lastIndexOf('#'));
				JsonArray lines = new JsonArray();
				for (String line : decompiledLines(entry)) {
					if (line.contains(needle)) {
						lines.add(line.trim());
						if (lines.size() >= MAX_DEC_LINES) {
							break;
						}
					}
				}
				if (lines.size() > 0) {
					JsonObject dl = new JsonObject();
					dl.addProperty("function", f);
					dl.add("lines", lines);
					dec.add(dl);
				}
			}
		}
		o.add("decompiled", dec);

		o.addProperty("region_sha", regionSha(a));
		return o;
	}

	/** Trim the evidence down to what a worklist needs. */
	private JsonObject summarise(JsonObject full) {
		JsonObject o = new JsonObject();
		for (String k : new String[] { "addr", "block", "segment", "symbol", "symbol_source",
				"type", "is_string", "span_to_next_anchor", "xref_count", "region_sha",
				"inside_defined_data" }) {
			if (full.has(k)) {
				o.add(k, full.get(k));
			}
		}
		o.addProperty("function_count", full.getAsJsonArray("referencing_functions").size());
		return o;
	}

	/** The pool ticket 14.2 settled for this function, or "untagged". */
	private String poolOf(Function f) {
		for (ghidra.program.model.listing.FunctionTag t : f.getTags()) {
			if (t.getName().startsWith("pool_")) {
				return t.getName().substring("pool_".length());
			}
		}
		return "untagged";
	}

	private String segmentOf(Address a) {
		MemoryBlock b = mem.getBlock(a);
		if (b != null && b.equals(codeBlock)) {
			return "object1_rodata";
		}
		if (b != null && ".object3".equals(b.getName())) {
			return "object3";
		}
		long off = a.getOffset();
		if (off < DATA_END) {
			return "CONST/_DATA";
		}
		if (off < BSS_END) {
			return "_BSS";
		}
		if (off >= STACK_START) {
			return "STACK";
		}
		return "align_pad";
	}

	private boolean isInitialised(Address a) {
		MemoryBlock b = mem.getBlock(a);
		if (b == null || !b.isInitialized()) {
			return false;
		}
		if (".object2".equals(b.getName()) && a.getOffset() >= OBJ2_FILE_END) {
			return false;
		}
		return true;
	}

	private String hexdump(Address a, int len) {
		StringBuilder sb = new StringBuilder();
		byte[] buf = new byte[len];
		try {
			mem.getBytes(a, buf);
		}
		catch (Exception e) {
			return "<unreadable: " + e.getMessage() + ">";
		}
		for (int i = 0; i < len; i += 16) {
			sb.append(String.format("%08x  ", a.getOffset() + i));
			StringBuilder ascii = new StringBuilder();
			for (int j = 0; j < 16; j++) {
				if (i + j < len) {
					sb.append(String.format("%02x ", buf[i + j]));
					char c = (char) (buf[i + j] & 0xff);
					ascii.append(c >= 0x20 && c < 0x7f ? c : '.');
				}
				else {
					sb.append("   ");
				}
			}
			sb.append(" |").append(ascii).append("|\n");
		}
		return sb.toString();
	}

	private String insnContext(Address at) {
		Instruction here = getInstructionAt(at);
		if (here == null) {
			return "";
		}
		List<Instruction> window = new ArrayList<>();
		Instruction cur = here;
		for (int i = 0; i < CONTEXT_INSNS && cur != null; i++) {
			cur = cur.getPrevious();
			if (cur != null) {
				window.add(0, cur);
			}
		}
		window.add(here);
		cur = here;
		for (int i = 0; i < CONTEXT_INSNS && cur != null; i++) {
			cur = cur.getNext();
			if (cur != null) {
				window.add(cur);
			}
		}
		StringBuilder sb = new StringBuilder();
		for (Instruction ins : window) {
			sb.append(ins.getAddress().equals(at) ? ">> " : "   ");
			sb.append(key(ins.getAddress())).append("  ").append(ins.toString()).append('\n');
		}
		return sb.toString();
	}

	private String[] decompiledLines(String entryKey) {
		String[] cached = decompCache.get(entryKey);
		if (cached != null) {
			return cached;
		}
		String[] out = new String[0];
		try {
			Function f = getFunctionAt(toAddr(entryKey));
			if (f != null) {
				DecompileResults r = decomp.decompileFunction(f, 60, monitor);
				if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
					out = r.getDecompiledFunction().getC().split("\n");
				}
			}
		}
		catch (Exception e) {
			out = new String[] { "<decompilation failed: " + e.getMessage() + ">" };
		}
		decompCache.put(entryKey, out);
		return out;
	}

	/**
	 * A fingerprint of the bytes this anchor sits on, which build_worklist.py
	 * compares so that a verdict written against different contents comes back
	 * onto the list instead of being skipped for ever (ADR-0007 5.7).
	 *
	 * The window is a fixed size on purpose. An earlier version hashed the span
	 * to the next anchor, which meant applying a type -- the whole point of this
	 * ticket -- absorbed the defined-data anchors inside it, lengthened the
	 * span, changed the hash and retired the verdict that had just been applied.
	 * The list would never have emptied. Staleness that comes from a neighbour
	 * swallowing this address is detected by build_worklist.py from
	 * inside_defined_data instead, which says so directly rather than by proxy.
	 */
	private String regionSha(Address a) {
		try {
			MessageDigest md = MessageDigest.getInstance("SHA-256");
			md.update((key(a) + ":").getBytes(StandardCharsets.UTF_8));
			if (isInitialised(a)) {
				MemoryBlock b = mem.getBlock(a);
				long room = b.getEnd().getOffset() - a.getOffset() + 1;
				int len = (int) Math.min(64L, room);
				byte[] buf = new byte[len];
				mem.getBytes(a, buf);
				md.update(buf);
			}
			StringBuilder sb = new StringBuilder();
			for (byte x : md.digest()) {
				sb.append(String.format("%02x", x));
			}
			return sb.substring(0, 16);
		}
		catch (Exception e) {
			return "?";
		}
	}

	private String key(Address a) {
		return String.format("%08x", a.getOffset());
	}

	private void writeJson(File f, JsonObject o) throws Exception {
		Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
		try (PrintWriter w = new PrintWriter(new OutputStreamWriter(
				new FileOutputStream(f), StandardCharsets.UTF_8))) {
			w.print(gson.toJson(o));
		}
	}
}
