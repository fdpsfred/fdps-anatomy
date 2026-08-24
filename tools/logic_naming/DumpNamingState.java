// Export everything a naming agent needs about one game-logic function, read-only.
//
// Ticket 15 gives every pool_fdps function a semantic name, semantic parameter
// names, a confirmed calling convention and a plate comment describing what it
// does. The judgement is made from assembly, so the agent needs the assembly --
// but it also needs the neighbourhood: who calls this, what it calls, which
// globals and strings it touches. Fetching that through the Ghidra MCP once per
// agent would serialise five hundred agents behind one database; dumping it once
// lets them run in parallel off files.
//
// Nothing here is a judgement. The dump states what the program contains and
// what Ghidra currently claims; which of those claims survive is the agent's
// call, and the transcription back into Ghidra is ApplyNamingVerdicts.java.
//
// Usage: run_ghidra_script with one argument:
//   <output dir>        writes functions.json, asm/, dec/, ctx/ under it
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
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;

public class DumpNamingState extends GhidraScript {

	/** Longest string literal reproduced in a context file. */
	private static final int MAX_STRING = 300;
	/** Plate comments are summarised to their first line in neighbour lists. */
	private static final int MAX_PLATE_HEAD = 240;

	private Memory mem;
	private Function[] byEntry;
	private DecompInterface decomp;

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: DumpNamingState <output dir>");
			return;
		}
		File out = new File(argv[0]);
		File asmDir = new File(out, "asm");
		File decDir = new File(out, "dec");
		File ctxDir = new File(out, "ctx");
		for (File d : new File[] { out, asmDir, decDir, ctxDir }) {
			d.mkdirs();
		}

		mem = currentProgram.getMemory();

		List<Function> all = new ArrayList<>();
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			all.add(it.next());
		}
		byEntry = all.toArray(new Function[0]);
		Arrays.sort(byEntry, (a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));

		decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		JsonArray functions = new JsonArray();
		int dumped = 0;
		for (int i = 0; i < byEntry.length; i++) {
			Function f = byEntry[i];
			if (!"fdps".equals(poolOf(f))) {
				continue;
			}
			functions.add(metaOf(f, i));
			writeText(new File(asmDir, key(f) + ".txt"), disassemble(f));
			writeText(new File(decDir, key(f) + ".c"), decompile(f));
			writeJson(new File(ctxDir, key(f) + ".json"), contextOf(f, i));
			dumped++;
			if (dumped % 50 == 0) {
				println("dumped " + dumped);
			}
		}
		decomp.dispose();

		JsonObject root = new JsonObject();
		root.addProperty("program", currentProgram.getName());
		root.addProperty("function_count", byEntry.length);
		root.addProperty("fdps_count", dumped);
		root.add("functions", functions);
		writeJson(new File(out, "functions.json"), root);

		println("pool_fdps functions dumped = " + dumped);
		println("output = " + out.getAbsolutePath());
	}

	// -------------------------------------------------------------- meta

	/**
	 * The one-line-per-function summary the packet builder works from: enough to
	 * decide whether a function still needs work and to check a stale verdict
	 * against the current body, without opening the assembly.
	 */
	private JsonObject metaOf(Function f, int index) throws Exception {
		JsonObject o = new JsonObject();
		AddressSetView body = f.getBody();
		o.addProperty("addr", key(f));
		o.addProperty("end", hex8(body.getMaxAddress().getOffset()));
		o.addProperty("size", body.getNumAddresses());
		o.addProperty("body_sha", bodyHash(body));
		o.addProperty("name", f.getName());
		o.addProperty("default_name", f.getName().startsWith("FUN_")
			|| f.getName().startsWith("thunk_FUN_"));
		o.addProperty("cc", String.valueOf(f.getCallingConventionName()));
		o.addProperty("prototype", f.getSignature().getPrototypeString(true));
		o.addProperty("thunk", f.isThunk());

		JsonArray params = new JsonArray();
		boolean defaultParam = false;
		for (Parameter p : f.getParameters()) {
			JsonObject pj = new JsonObject();
			pj.addProperty("name", p.getName());
			pj.addProperty("type", p.getDataType().getName());
			pj.addProperty("storage", p.getVariableStorage().toString());
			params.add(pj);
			if (p.getName().matches("param_\\d+")) {
				defaultParam = true;
			}
		}
		o.add("params", params);
		o.addProperty("default_params", defaultParam);

		String plate = getPlateComment(f.getEntryPoint());
		o.addProperty("plate_len", plate == null ? 0 : plate.length());

		JsonArray tags = new JsonArray();
		for (FunctionTag t : f.getTags()) {
			tags.add(t.getName());
		}
		o.add("tags", tags);

		o.addProperty("caller_count", f.getCallingFunctions(monitor).size());
		o.addProperty("callee_count", f.getCalledFunctions(monitor).size());
		return o;
	}

	// ----------------------------------------------------------- context

	/**
	 * The neighbourhood. A function's purpose is rarely legible from its own
	 * bytes alone -- what named code calls it, what named code it calls, and
	 * which strings and globals it touches are usually what settles the name.
	 */
	private JsonObject contextOf(Function f, int index) throws Exception {
		JsonObject o = new JsonObject();
		o.addProperty("addr", key(f));
		o.addProperty("name", f.getName());
		o.addProperty("size", f.getBody().getNumAddresses());
		o.addProperty("cc", String.valueOf(f.getCallingConventionName()));
		o.addProperty("prototype", f.getSignature().getPrototypeString(true));

		String plate = getPlateComment(f.getEntryPoint());
		o.addProperty("current_plate", plate == null ? "" : plate);

		// Holes and the gap to the next function: where a fall-through tail that
		// should have been folded in would sit, and where dead code hides.
		Address entry = f.getEntryPoint();
		Address last = f.getBody().getMaxAddress();
		JsonArray holes = new JsonArray();
		AddressSet missing = new AddressSet(entry, last).subtract(new AddressSet(f.getBody()));
		AddressRangeIterator hi = missing.getAddressRanges();
		while (hi.hasNext()) {
			AddressRange r = hi.next();
			holes.add(hex8(r.getMinAddress().getOffset()) + "-" + hex8(r.getMaxAddress().getOffset()));
		}
		o.add("body_holes", holes);

		Function prev = index > 0 ? byEntry[index - 1] : null;
		Function next = index + 1 < byEntry.length ? byEntry[index + 1] : null;
		o.addProperty("prev_fn", prev == null ? "" : describe(prev));
		o.addProperty("next_fn", next == null ? "" : describe(next));
		long gapStart = last.getOffset() + 1;
		long gapEnd = next == null ? gapStart - 1 : next.getEntryPoint().getOffset() - 1;
		o.addProperty("gap_to_next", Math.max(0, gapEnd - gapStart + 1));

		JsonArray callers = new JsonArray();
		for (Function c : sorted(f.getCallingFunctions(monitor))) {
			callers.add(neighbour(c));
		}
		o.add("callers", callers);

		JsonArray callees = new JsonArray();
		for (Function c : sorted(f.getCalledFunctions(monitor))) {
			callees.add(neighbour(c));
		}
		o.add("callees", callees);

		// Everything that points at the entry which is not a call: a function
		// pointer table slot, a jump from elsewhere, a data reference.
		JsonArray nonCallRefs = new JsonArray();
		ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(entry);
		while (ri.hasNext()) {
			Reference r = ri.next();
			RefType t = r.getReferenceType();
			if (t.isCall()) {
				continue;
			}
			JsonObject rj = new JsonObject();
			rj.addProperty("from", hex8(r.getFromAddress().getOffset()));
			rj.addProperty("type", t.getName());
			Function inside = getFunctionContaining(r.getFromAddress());
			rj.addProperty("in", inside == null ? "" : describe(inside));
			nonCallRefs.add(rj);
		}
		o.add("non_call_refs_to_entry", nonCallRefs);

		o.add("data_refs", dataRefs(f));
		return o;
	}

	/**
	 * Data and string references made from the body, deduplicated by target.
	 * The string literals are the single most productive naming evidence in this
	 * program: filenames, asset names and messages say what the code is for.
	 */
	private JsonArray dataRefs(Function f) {
		TreeMap<String, JsonObject> byTarget = new TreeMap<>();
		InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
		while (ii.hasNext()) {
			Instruction insn = ii.next();
			for (Reference r : insn.getReferencesFrom()) {
				RefType t = r.getReferenceType();
				if (t.isCall() || t.isFlow()) {
					continue;
				}
				Address to = r.getToAddress();
				if (to == null || !mem.contains(to)) {
					continue;
				}
				String tk = hex8(to.getOffset());
				JsonObject d = byTarget.get(tk);
				if (d == null) {
					d = new JsonObject();
					d.addProperty("addr", tk);
					Symbol s = getSymbolAt(to);
					d.addProperty("symbol", s == null ? "" : s.getName());
					Data data = getDataAt(to);
					d.addProperty("type", data == null ? "" : data.getDataType().getName());
					d.addProperty("string", stringAt(to));
					d.addProperty("refs", 0);
					byTarget.put(tk, d);
				}
				d.addProperty("refs", d.get("refs").getAsInt() + 1);
			}
		}
		JsonArray out = new JsonArray();
		for (JsonObject d : byTarget.values()) {
			out.add(d);
		}
		return out;
	}

	private JsonObject neighbour(Function f) {
		JsonObject o = new JsonObject();
		o.addProperty("addr", key(f));
		o.addProperty("name", f.getName());
		o.addProperty("pool", poolOf(f));
		o.addProperty("prototype", f.getSignature().getPrototypeString(true));
		String plate = getPlateComment(f.getEntryPoint());
		o.addProperty("plate_head", plateHead(plate));
		return o;
	}

	/** The first line of a plate comment, which is where its summary sentence is. */
	private static String plateHead(String plate) {
		if (plate == null || plate.isEmpty()) {
			return "";
		}
		int nl = plate.indexOf('\n');
		String head = nl < 0 ? plate : plate.substring(0, nl);
		head = head.trim();
		return head.length() > MAX_PLATE_HEAD ? head.substring(0, MAX_PLATE_HEAD) + "..." : head;
	}

	// ------------------------------------------------------------- text

	private String disassemble(Function f) {
		StringBuilder sb = new StringBuilder();
		sb.append("; ").append(f.getName()).append("  ").append(key(f))
			.append('-').append(hex8(f.getBody().getMaxAddress().getOffset()))
			.append("  ").append(f.getBody().getNumAddresses()).append(" bytes\n");
		InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
		while (ii.hasNext()) {
			Instruction insn = ii.next();
			Address a = insn.getAddress();
			// A label at this address means something jumps or falls here; without
			// it the control flow in a long body is unreadable.
			Symbol s = getSymbolAt(a);
			if (s != null && s.getSource() != ghidra.program.model.symbol.SourceType.DEFAULT
				&& !a.equals(f.getEntryPoint())) {
				sb.append(s.getName()).append(":\n");
			}
			sb.append(hex8(a.getOffset())).append("  ");
			sb.append(pad(insn.toString(), 44));
			String cmt = insn.getComment(CodeUnit.EOL_COMMENT);
			if (cmt != null && !cmt.isEmpty()) {
				sb.append("  ; ").append(cmt.replace("\n", " "));
			}
			// Spell out what an operand address resolves to; "CALL 0x2d210" alone
			// hides that the callee already has a name.
			String ann = annotate(insn);
			if (!ann.isEmpty()) {
				sb.append("  ; ").append(ann);
			}
			sb.append('\n');
		}
		return sb.toString();
	}

	private String annotate(Instruction insn) {
		StringBuilder sb = new StringBuilder();
		Set<String> seen = new HashSet<>();
		for (Reference r : insn.getReferencesFrom()) {
			Address to = r.getToAddress();
			if (to == null || !mem.contains(to)) {
				continue;
			}
			Symbol s = getSymbolAt(to);
			String label = s == null ? "" : s.getName();
			String str = stringAt(to);
			if (label.isEmpty() && str.isEmpty()) {
				continue;
			}
			String piece = label.isEmpty() ? "\"" + str + "\""
				: (str.isEmpty() ? label : label + " = \"" + str + "\"");
			if (seen.add(piece)) {
				if (sb.length() > 0) {
					sb.append(", ");
				}
				sb.append(piece);
			}
		}
		return sb.toString();
	}

	private String decompile(Function f) {
		try {
			DecompileResults res = decomp.decompileFunction(f, 90, monitor);
			if (res == null || !res.decompileCompleted() || res.getDecompiledFunction() == null) {
				return "/* decompilation failed: "
					+ (res == null ? "no result" : res.getErrorMessage()) + " */\n";
			}
			return res.getDecompiledFunction().getC();
		}
		catch (Exception e) {
			return "/* decompilation threw " + e.getClass().getSimpleName() + ": "
				+ e.getMessage() + " */\n";
		}
	}

	// ------------------------------------------------------------ utils

	private String stringAt(Address a) {
		Data d = getDataAt(a);
		if (d != null) {
			StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
			if (sdi != null && sdi != StringDataInstance.NULL_INSTANCE) {
				String v = sdi.getStringValue();
				if (v != null && v.length() >= 3) {
					return trimString(v);
				}
			}
		}
		// Undefined bytes that read as text are still text; a lot of this program's
		// filenames were never given a data type.
		try {
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < MAX_STRING; i++) {
				int c = mem.getByte(a.add(i)) & 0xff;
				if (c == 0) {
					break;
				}
				if (c < 0x20 || c > 0x7e) {
					return "";
				}
				sb.append((char) c);
			}
			return sb.length() >= 4 ? trimString(sb.toString()) : "";
		}
		catch (Exception e) {
			return "";
		}
	}

	private static String trimString(String s) {
		String out = s.replace("\n", "\\n").replace("\r", "\\r").replace("\"", "'");
		return out.length() > MAX_STRING ? out.substring(0, MAX_STRING) + "..." : out;
	}

	private String poolOf(Function f) {
		for (FunctionTag t : f.getTags()) {
			if (t.getName().startsWith("pool_")) {
				return t.getName().substring(5);
			}
		}
		return "";
	}

	private String describe(Function f) {
		return f.getName() + "@" + key(f) + "-" + hex8(f.getBody().getMaxAddress().getOffset());
	}

	private List<Function> sorted(Set<Function> set) {
		List<Function> out = new ArrayList<>(set);
		out.sort((a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));
		return out;
	}

	private String bodyHash(AddressSetView body) throws Exception {
		MessageDigest md = MessageDigest.getInstance("SHA-256");
		AddressRangeIterator ri = body.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			int len = (int) r.getLength();
			byte[] buf = new byte[len];
			try {
				mem.getBytes(r.getMinAddress(), buf);
			}
			catch (Exception e) {
				md.update("unreadable".getBytes(StandardCharsets.UTF_8));
				continue;
			}
			md.update(hex8(r.getMinAddress().getOffset()).getBytes(StandardCharsets.UTF_8));
			md.update(buf);
		}
		byte[] digest = md.digest();
		StringBuilder sb = new StringBuilder();
		for (int i = 0; i < 8; i++) {
			sb.append(Character.forDigit((digest[i] >> 4) & 0xf, 16));
			sb.append(Character.forDigit(digest[i] & 0xf, 16));
		}
		return sb.toString();
	}

	private static String key(Function f) {
		return hex8(f.getEntryPoint().getOffset());
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private static String pad(String s, int width) {
		StringBuilder sb = new StringBuilder(s);
		while (sb.length() < width) {
			sb.append(' ');
		}
		return sb.toString();
	}

	private void writeText(File f, String text) throws Exception {
		try (PrintWriter pw = new PrintWriter(new OutputStreamWriter(
			new FileOutputStream(f), StandardCharsets.UTF_8))) {
			pw.print(text);
		}
	}

	private void writeJson(File f, Object value) throws Exception {
		Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
		writeText(f, gson.toJson(value));
	}
}
