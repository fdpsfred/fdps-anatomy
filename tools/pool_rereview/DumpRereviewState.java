// Dump everything ticket 14.2 needs to re-read every function in FDPS.LE.
//
// The re-review asks four independent questions per function -- pool, name,
// boundary and signature, plate comment -- and ADR-0002 requires each of them to
// be answered from the assembly rather than from the tag that is already there.
// So the dump is deliberately split in two:
//
//   functions.json  the facts: body ranges, bytes, call edges, references,
//                   neighbours, the gap after the body, holes inside it.
//   current.json    what Ghidra currently claims: name, pool tags, signature,
//                   plate comment. build_packets.py keeps this in a separate
//                   file so an agent can be told to read it only after it has
//                   formed its own conclusion.
//   asm/<addr>.txt  the disassembly of the function, plus a short tail past the
//                   end of its body so a missing fall-through is visible.
//
// Read-only: the script never writes to the program database.
//
// Usage (Ghidra MCP): run_ghidra_script with this absolute path. Optional
// argument: the output directory (default workspace/pool_rereview).
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
import java.util.Collection;
import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnitFormat;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class DumpRereviewState extends GhidraScript {

	private static final String EXPECTED_PROGRAM = "FDPS.LE";
	private static final String DEFAULT_OUT_DIR =
		"C:/Users/fdpsf/Documents/fdps-anatomy/workspace/pool_rereview";
	private static final String CODE_BLOCK = ".object1";

	private static final int HEAD_BYTES = 32;
	private static final int MAX_REFS = 24;
	private static final int TAIL_INSTRUCTIONS = 6;
	private static final int MAX_STRING = 120;

	private long[] entries;
	private Function[] byEntry;
	private Memory mem;
	private Listing listing;
	private ReferenceManager refs;
	private SymbolTable symbols;
	private CodeUnitFormat fmt;

	@Override
	public void run() throws Exception {
		if (currentProgram == null || !EXPECTED_PROGRAM.equals(currentProgram.getName())) {
			throw new IllegalStateException("current program is " +
				(currentProgram == null ? "(none)" : currentProgram.getName()) +
				", expected " + EXPECTED_PROGRAM);
		}
		String[] argv = getScriptArgs();
		File outDir = new File(argv.length > 0 ? argv[0] : DEFAULT_OUT_DIR);
		File asmDir = new File(outDir, "asm");
		outDir.mkdirs();
		asmDir.mkdirs();

		mem = currentProgram.getMemory();
		listing = currentProgram.getListing();
		refs = currentProgram.getReferenceManager();
		symbols = currentProgram.getSymbolTable();
		fmt = CodeUnitFormat.DEFAULT;

		indexFunctions();

		JsonArray facts = new JsonArray();
		JsonArray current = new JsonArray();
		int asmWritten = 0;
		for (int i = 0; i < byEntry.length; i++) {
			Function f = byEntry[i];
			facts.add(factsOf(f, i));
			current.add(currentOf(f));
			write(new File(asmDir, hex8(f.getEntryPoint().getOffset()) + ".txt"), disassemble(f));
			asmWritten++;
		}

		write(new File(outDir, "functions.json"), facts.toString() + "\n");
		write(new File(outDir, "current.json"), current.toString() + "\n");

		MemoryBlock code = mem.getBlock(CODE_BLOCK);
		JsonObject meta = new JsonObject();
		meta.addProperty("functions", byEntry.length);
		meta.addProperty("code_start", hex8(code.getStart().getOffset()));
		meta.addProperty("code_end", hex8(code.getEnd().getOffset()));
		write(new File(outDir, "dump_meta.json"), meta.toString() + "\n");

		println("functions = " + byEntry.length);
		println("asm files = " + asmWritten);
		println("written to " + outDir.getAbsolutePath());
	}

	private void indexFunctions() {
		FunctionManager fm = currentProgram.getFunctionManager();
		List<Function> all = new ArrayList<>();
		FunctionIterator it = fm.getFunctions(true);
		while (it.hasNext()) {
			all.add(it.next());
		}
		entries = new long[all.size()];
		byEntry = new Function[all.size()];
		for (int i = 0; i < all.size(); i++) {
			entries[i] = all.get(i).getEntryPoint().getOffset();
			byEntry[i] = all.get(i);
		}
	}

	// ------------------------------------------------------------- facts

	private JsonObject factsOf(Function f, int index) throws Exception {
		JsonObject o = new JsonObject();
		Address entry = f.getEntryPoint();
		AddressSetView body = f.getBody();
		Address last = body.getMaxAddress();

		o.addProperty("addr", hex8(entry.getOffset()));
		o.addProperty("end", hex8(last.getOffset()));
		o.addProperty("size", body.getNumAddresses());

		JsonArray ranges = new JsonArray();
		AddressRangeIterator ri = body.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			ranges.add(hex8(r.getMinAddress().getOffset()) + "-" + hex8(r.getMaxAddress().getOffset()));
		}
		o.add("body_ranges", ranges);
		o.addProperty("body_sha", bodyHash(body));

		// Holes: addresses between the entry and the last body byte that the body
		// does not own. A hole is usually dead code the compiler emitted after a
		// no-return call, and it is the shape that made ticket 14's boundary
		// judgements go stale.
		JsonArray holes = new JsonArray();
		AddressSet span = new AddressSet(entry, last);
		AddressSet missing = span.subtract(new AddressSet(body));
		AddressRangeIterator hi = missing.getAddressRanges();
		while (hi.hasNext()) {
			AddressRange r = hi.next();
			JsonObject h = new JsonObject();
			h.addProperty("range", hex8(r.getMinAddress().getOffset()) + "-"
				+ hex8(r.getMaxAddress().getOffset()));
			h.addProperty("size", r.getLength());
			h.addProperty("hex", bytesHex(r.getMinAddress(), (int) Math.min(r.getLength(), 32)));
			holes.add(h);
		}
		o.add("body_holes", holes);

		o.addProperty("head_hex", bytesHex(entry, (int) Math.min(body.getNumAddresses(), HEAD_BYTES)));
		o.addProperty("thunk", f.isThunk());
		Function thunked = f.getThunkedFunction(false);
		o.addProperty("thunk_target", thunked == null ? ""
			: hex8(thunked.getEntryPoint().getOffset()));

		Function prev = index > 0 ? byEntry[index - 1] : null;
		Function next = index + 1 < byEntry.length ? byEntry[index + 1] : null;
		o.addProperty("prev_fn", prev == null ? ""
			: prev.getName() + "@" + hex8(prev.getEntryPoint().getOffset())
				+ "-" + hex8(prev.getBody().getMaxAddress().getOffset()));
		o.addProperty("next_fn", next == null ? ""
			: next.getName() + "@" + hex8(next.getEntryPoint().getOffset()));

		// The gap between the end of this body and the next entry. A non-empty gap
		// is where a fall-through tail that should have been folded in would sit.
		JsonObject gap = new JsonObject();
		long gapStart = last.getOffset() + 1;
		long gapEnd = next == null ? gapStart - 1 : next.getEntryPoint().getOffset() - 1;
		long gapSize = gapEnd - gapStart + 1;
		gap.addProperty("size", Math.max(0, gapSize));
		if (gapSize > 0 && gapSize < 4096) {
			Address gs = toAddr(gapStart);
			gap.addProperty("start", hex8(gapStart));
			gap.addProperty("hex", bytesHex(gs, (int) Math.min(gapSize, 48)));
			gap.addProperty("has_instructions", listing.getInstructionAt(gs) != null);
			Data d = listing.getDataAt(gs);
			gap.addProperty("defined_data", d != null && d.isDefined());
		}
		o.add("gap_after", gap);

		o.add("callers", edgeArray(f.getCallingFunctions(monitor)));
		o.add("callees", edgeArray(f.getCalledFunctions(monitor)));

		// Everything pointing at the entry, including data references: a function
		// reached only through a pointer table has no caller at all, and then the
		// dispatcher decides its pool.
		JsonArray to = new JsonArray();
		int n = 0;
		for (Reference r : refs.getReferencesTo(entry)) {
			if (n++ >= MAX_REFS) {
				break;
			}
			JsonObject e = new JsonObject();
			Address from = r.getFromAddress();
			e.addProperty("from", hex8(from.getOffset()));
			e.addProperty("type", r.getReferenceType().getName());
			Function owner = getFunctionContaining(from);
			e.addProperty("in", owner == null ? describeData(from) : owner.getName());
			to.add(e);
		}
		o.add("refs_to", to);
		o.addProperty("refs_to_total", countRefsTo(entry));

		o.add("data_refs", dataRefs(f));
		return o;
	}

	/** Data the function reads or writes, with the string contents when there are any. */
	private JsonArray dataRefs(Function f) {
		JsonArray out = new JsonArray();
		int n = 0;
		AddressRangeIterator ri = f.getBody().getAddressRanges();
		while (ri.hasNext() && n < MAX_REFS) {
			AddressRange r = ri.next();
			for (Address a = r.getMinAddress(); a.compareTo(r.getMaxAddress()) <= 0; ) {
				for (Reference ref : refs.getReferencesFrom(a)) {
					if (ref.getReferenceType().isCall() || ref.getReferenceType().isJump()) {
						continue;
					}
					Address target = ref.getToAddress();
					if (f.getBody().contains(target)) {
						continue;
					}
					// Stack and register references carry synthetic addresses that
					// are not in any memory block; they say nothing about which
					// module the code belongs to.
					if (!mem.contains(target)) {
						continue;
					}
					if (n++ >= MAX_REFS) {
						break;
					}
					JsonObject e = new JsonObject();
					e.addProperty("from", hex8(a.getOffset()));
					e.addProperty("to", hex8(target.getOffset()));
					e.addProperty("type", ref.getReferenceType().getName());
					Symbol s = symbols.getPrimarySymbol(target);
					e.addProperty("label", s == null ? "" : s.getName());
					String str = readString(target);
					if (!str.isEmpty()) {
						e.addProperty("string", str);
					}
					out.add(e);
				}
				Instruction ins = listing.getInstructionAt(a);
				long step = ins == null ? 1 : ins.getLength();
				try {
					a = a.add(step);
				}
				catch (Exception ex) {
					break;
				}
			}
		}
		return out;
	}

	// ----------------------------------------------------------- current

	private JsonObject currentOf(Function f) {
		JsonObject o = new JsonObject();
		Address entry = f.getEntryPoint();
		o.addProperty("addr", hex8(entry.getOffset()));
		o.addProperty("name", f.getName());
		o.addProperty("cc", String.valueOf(f.getCallingConventionName()));
		o.addProperty("sig_source", String.valueOf(f.getSignatureSource()));
		o.addProperty("prototype", f.getSignature().getPrototypeString(true));
		o.addProperty("return_type", f.getReturnType().getDisplayName());
		o.addProperty("varargs", f.hasVarArgs());
		o.addProperty("no_return", f.hasNoReturn());
		o.addProperty("stack_purge", f.getStackPurgeSize());
		JsonArray params = new JsonArray();
		for (Parameter p : f.getParameters()) {
			JsonObject e = new JsonObject();
			e.addProperty("name", p.getName());
			e.addProperty("type", p.getDataType().getDisplayName());
			e.addProperty("storage", p.getVariableStorage().toString());
			params.add(e);
		}
		o.add("params", params);
		JsonArray tags = new JsonArray();
		for (FunctionTag t : f.getTags()) {
			tags.add(t.getName());
		}
		o.add("tags", tags);
		String plate = getPlateComment(entry);
		o.addProperty("plate", plate == null ? "" : plate);
		return o;
	}

	// ------------------------------------------------------- disassembly

	private String disassemble(Function f) {
		StringBuilder sb = new StringBuilder();
		AddressSetView body = f.getBody();
		sb.append("; body ").append(hex8(f.getEntryPoint().getOffset())).append('-')
			.append(hex8(body.getMaxAddress().getOffset()))
			.append("  ").append(body.getNumAddresses()).append(" bytes\n");

		AddressRangeIterator ri = body.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			sb.append("; range ").append(hex8(r.getMinAddress().getOffset())).append('-')
				.append(hex8(r.getMaxAddress().getOffset())).append('\n');
			Address a = r.getMinAddress();
			while (a != null && a.compareTo(r.getMaxAddress()) <= 0) {
				Instruction ins = listing.getInstructionAt(a);
				if (ins == null) {
					Data d = listing.getDataAt(a);
					int len = d == null ? 1 : d.getLength();
					sb.append(hex8(a.getOffset())).append("  ")
						.append(pad(bytesHex(a, Math.min(len, 8)), 20)).append("  ")
						.append(d != null && d.isDefined() ? d.getDataType().getName() : "??")
						.append('\n');
					a = advance(a, len);
					continue;
				}
				sb.append(line(ins));
				a = advance(a, ins.getLength());
			}
		}

		// A few instructions past the end, so an unfolded tail is visible without
		// the agent having to go and ask Ghidra for the neighbourhood.
		Address after = advance(body.getMaxAddress(), 1);
		if (after != null) {
			sb.append("; --- after the body (not part of this function) ---\n");
			for (int i = 0; i < TAIL_INSTRUCTIONS && after != null; i++) {
				Instruction ins = listing.getInstructionAt(after);
				if (ins == null) {
					sb.append(hex8(after.getOffset())).append("  ")
						.append(pad(bytesHex(after, 8), 20)).append("  (not disassembled)\n");
					break;
				}
				Function owner = getFunctionContaining(after);
				sb.append(line(ins));
				if (owner != null && owner.getEntryPoint().equals(after)) {
					sb.append("; ^ entry of ").append(owner.getName()).append('\n');
				}
				after = advance(after, ins.getLength());
			}
		}
		return sb.toString();
	}

	private String line(Instruction ins) {
		StringBuilder sb = new StringBuilder();
		Address a = ins.getAddress();
		sb.append(hex8(a.getOffset())).append("  ")
			.append(pad(bytesHex(a, ins.getLength()), 20)).append("  ")
			.append(fmt.getRepresentationString(ins)).append('\n');
		return sb.toString();
	}

	// ----------------------------------------------------------- helpers

	private JsonArray edgeArray(Collection<Function> fns) {
		JsonArray out = new JsonArray();
		int i = 0;
		for (Function f : fns) {
			if (i++ >= 16) {
				break;
			}
			out.add(f.getName() + "@" + hex8(f.getEntryPoint().getOffset()));
		}
		return out;
	}

	private int countRefsTo(Address a) {
		int n = 0;
		for (@SuppressWarnings("unused") Reference r : refs.getReferencesTo(a)) {
			n++;
		}
		return n;
	}

	private String describeData(Address from) {
		Symbol s = symbols.getPrimarySymbol(from);
		if (s != null) {
			return "data:" + s.getName();
		}
		Data d = listing.getDataContaining(from);
		if (d != null) {
			Symbol ds = symbols.getPrimarySymbol(d.getMinAddress());
			if (ds != null) {
				return "data:" + ds.getName() + "+" + (from.getOffset() - d.getMinAddress().getOffset());
			}
		}
		return "data";
	}

	private String readString(Address a) {
		Data d = listing.getDataAt(a);
		if (d != null && d.isDefined() && d.getValue() instanceof String) {
			return trim((String) d.getValue());
		}
		try {
			byte[] buf = new byte[MAX_STRING];
			int got = mem.getBytes(a, buf);
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < got; i++) {
				int c = buf[i] & 0xff;
				if (c == 0) {
					break;
				}
				if (c < 0x20 || c > 0x7e) {
					return "";
				}
				sb.append((char) c);
			}
			return sb.length() >= 4 ? trim(sb.toString()) : "";
		}
		catch (Exception e) {
			return "";
		}
	}

	private static String trim(String s) {
		String out = s.replace("\n", "\\n").replace("\r", "\\r");
		return out.length() > MAX_STRING ? out.substring(0, MAX_STRING) + "..." : out;
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

	private String bytesHex(Address a, int len) {
		if (len <= 0) {
			return "";
		}
		byte[] buf = new byte[len];
		try {
			mem.getBytes(a, buf);
		}
		catch (Exception e) {
			return "";
		}
		StringBuilder sb = new StringBuilder();
		for (byte b : buf) {
			sb.append(Character.forDigit((b >> 4) & 0xf, 16));
			sb.append(Character.forDigit(b & 0xf, 16));
		}
		return sb.toString();
	}

	private Address advance(Address a, long by) {
		try {
			return a.add(by);
		}
		catch (Exception e) {
			return null;
		}
	}

	private static String pad(String s, int width) {
		StringBuilder sb = new StringBuilder(s);
		while (sb.length() < width) {
			sb.append(' ');
		}
		return sb.toString();
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private static void write(File f, String text) throws Exception {
		try (PrintWriter pw = new PrintWriter(
			new OutputStreamWriter(new FileOutputStream(f), StandardCharsets.UTF_8))) {
			pw.print(text);
		}
	}
}
