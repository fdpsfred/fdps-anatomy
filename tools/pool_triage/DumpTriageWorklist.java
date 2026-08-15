// Dump the two fixed worklists ticket 14 works from, plus the raw code image.
//
//   blocks.json     every undefined range inside .object1, classified only by
//                   whether anything references it and whether it is short
//                   enough to be alignment filler. No judgement of content:
//                   that is what the per-block agents are for.
//   functions.json  every function: entry, size, name, convention, tags,
//                   caller/callee counts and the first bytes of the prologue.
//   object1.bin     the loaded image of the code object, for the offline
//                   Watcom library byte matcher.
//
// Read-only: the script never writes to the program database.
//
// Usage (Ghidra MCP): run_ghidra_script with this absolute path. Optional
// argument: the output directory (default workspace/pool_triage).
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Reference;

public class DumpTriageWorklist extends GhidraScript {

	private static final String EXPECTED_PROGRAM = "FDPS.LE";
	private static final String DEFAULT_OUT_DIR =
		"C:/Users/fdpsf/Documents/fdps-anatomy/workspace/pool_triage";
	private static final String CODE_BLOCK = ".object1";
	private static final int PREVIEW_BYTES = 48;

	private long[] entries;
	private Function[] byEntry;

	@Override
	public void run() throws Exception {
		if (currentProgram == null || !EXPECTED_PROGRAM.equals(currentProgram.getName())) {
			throw new IllegalStateException("current program is " +
				(currentProgram == null ? "(none)" : currentProgram.getName()) +
				", expected " + EXPECTED_PROGRAM);
		}
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : DEFAULT_OUT_DIR);
		outDir.mkdirs();

		indexFunctions();
		int blocks = dumpBlocks(outDir);
		int functions = dumpFunctions(outDir);
		long imageBytes = dumpImage(outDir);

		println("blocks = " + blocks);
		println("functions = " + functions);
		println("object1.bin = " + imageBytes + " bytes");
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

	/** The function whose entry point is the greatest one below addr, or null. */
	private Function functionBefore(long addr) {
		int lo = 0;
		int hi = entries.length - 1;
		int best = -1;
		while (lo <= hi) {
			int mid = (lo + hi) >>> 1;
			if (entries[mid] < addr) {
				best = mid;
				lo = mid + 1;
			}
			else {
				hi = mid - 1;
			}
		}
		return best < 0 ? null : byEntry[best];
	}

	private Function functionAfter(long addr) {
		int lo = 0;
		int hi = entries.length - 1;
		int best = -1;
		while (lo <= hi) {
			int mid = (lo + hi) >>> 1;
			if (entries[mid] > addr) {
				best = mid;
				hi = mid - 1;
			}
			else {
				lo = mid + 1;
			}
		}
		return best < 0 ? null : byEntry[best];
	}

	private int dumpBlocks(File outDir) throws Exception {
		Memory mem = currentProgram.getMemory();
		MemoryBlock code = mem.getBlock(CODE_BLOCK);
		Listing listing = currentProgram.getListing();
		ReferenceManager rm = currentProgram.getReferenceManager();

		AddressSet undefined = new AddressSet();
		DataIterator di = listing.getData(new AddressSet(code.getStart(), code.getEnd()), true);
		while (di.hasNext()) {
			Data d = di.next();
			if (!d.isDefined()) {
				undefined.addRange(d.getMinAddress(), d.getMaxAddress());
			}
		}

		StringBuilder sb = new StringBuilder("[\n");
		int count = 0;
		AddressRangeIterator ri = undefined.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			long len = r.getLength();
			byte[] preview = new byte[(int) Math.min(len, PREVIEW_BYTES)];
			mem.getBytes(r.getMinAddress(), preview);
			byte[] tail = new byte[(int) Math.min(len, 16)];
			mem.getBytes(r.getMaxAddress().subtract(tail.length - 1), tail);

			List<String> refs = new ArrayList<>();
			for (Address a = r.getMinAddress(); a.compareTo(r.getMaxAddress()) <= 0; a = a.add(1)) {
				for (Reference ref : rm.getReferencesTo(a)) {
					refs.add(ref.getFromAddress() + "->" + a);
					if (refs.size() >= 8) {
						break;
					}
				}
				if (refs.size() >= 8) {
					break;
				}
			}

			Function prev = functionBefore(r.getMinAddress().getOffset());
			Function next = functionAfter(r.getMinAddress().getOffset());
			boolean adjacentToPrev = prev != null &&
				prev.getBody().getMaxAddress().getOffset() + 1 == r.getMinAddress().getOffset();
			boolean adjacentToNext = next != null &&
				next.getEntryPoint().getOffset() == r.getMaxAddress().getOffset() + 1;

			if (count > 0) {
				sb.append(",\n");
			}
			sb.append("  {");
			sb.append("\"start\":\"").append(hex8(r.getMinAddress().getOffset())).append("\",");
			sb.append("\"end\":\"").append(hex8(r.getMaxAddress().getOffset())).append("\",");
			sb.append("\"size\":").append(len).append(",");
			sb.append("\"referenced\":").append(refs.isEmpty() ? "false" : "true").append(",");
			sb.append("\"refs\":[");
			for (int i = 0; i < refs.size(); i++) {
				sb.append(i > 0 ? "," : "").append('"').append(refs.get(i)).append('"');
			}
			sb.append("],");
			sb.append("\"prev_fn\":").append(prev == null ? "null"
				: "\"" + esc(prev.getName()) + "@" + hex8(prev.getEntryPoint().getOffset())
					+ "-" + hex8(prev.getBody().getMaxAddress().getOffset()) + "\"").append(",");
			sb.append("\"next_fn\":").append(next == null ? "null"
				: "\"" + esc(next.getName()) + "@" + hex8(next.getEntryPoint().getOffset()) + "\"")
				.append(",");
			sb.append("\"touches_prev_body\":").append(adjacentToPrev).append(",");
			sb.append("\"touches_next_entry\":").append(adjacentToNext).append(",");
			sb.append("\"head_hex\":\"").append(toHex(preview)).append("\",");
			sb.append("\"tail_hex\":\"").append(toHex(tail)).append("\"");
			sb.append("}");
			count++;
		}
		sb.append("\n]\n");
		write(new File(outDir, "blocks.json"), sb.toString());
		return count;
	}

	private int dumpFunctions(File outDir) throws Exception {
		Memory mem = currentProgram.getMemory();
		StringBuilder sb = new StringBuilder("[\n");
		int count = 0;
		for (Function f : byEntry) {
			long entry = f.getEntryPoint().getOffset();
			long size = f.getBody().getNumAddresses();
			byte[] head = new byte[(int) Math.min(size, 16)];
			try {
				mem.getBytes(f.getEntryPoint(), head);
			}
			catch (Exception e) {
				head = new byte[0];
			}
			Collection<Function> callers = f.getCallingFunctions(monitor);
			Collection<Function> callees = f.getCalledFunctions(monitor);
			List<String> tags = new ArrayList<>();
			for (FunctionTag t : f.getTags()) {
				tags.add(t.getName());
			}
			java.util.Collections.sort(tags);

			if (count > 0) {
				sb.append(",\n");
			}
			sb.append("  {");
			sb.append("\"addr\":\"").append(hex8(entry)).append("\",");
			sb.append("\"name\":\"").append(esc(f.getName())).append("\",");
			sb.append("\"size\":").append(size).append(",");
			sb.append("\"end\":\"").append(hex8(f.getBody().getMaxAddress().getOffset())).append("\",");
			sb.append("\"cc\":\"").append(esc(String.valueOf(f.getCallingConventionName()))).append("\",");
			sb.append("\"sig_source\":\"").append(f.getSignatureSource()).append("\",");
			sb.append("\"callers\":").append(callers.size()).append(",");
			sb.append("\"callees\":").append(callees.size()).append(",");
			sb.append("\"caller_names\":").append(names(callers)).append(",");
			sb.append("\"callee_names\":").append(names(callees)).append(",");
			sb.append("\"thunk\":").append(f.isThunk()).append(",");
			sb.append("\"tags\":[");
			for (int i = 0; i < tags.size(); i++) {
				sb.append(i > 0 ? "," : "").append('"').append(esc(tags.get(i))).append('"');
			}
			sb.append("],");
			sb.append("\"head_hex\":\"").append(toHex(head)).append("\"");
			sb.append("}");
			count++;
		}
		sb.append("\n]\n");
		write(new File(outDir, "functions.json"), sb.toString());
		return count;
	}

	private long dumpImage(File outDir) throws Exception {
		Memory mem = currentProgram.getMemory();
		MemoryBlock code = mem.getBlock(CODE_BLOCK);
		int size = (int) code.getSize();
		byte[] buf = new byte[size];
		mem.getBytes(code.getStart(), buf);
		try (FileOutputStream fos = new FileOutputStream(new File(outDir, "object1.bin"))) {
			fos.write(buf);
		}
		write(new File(outDir, "object1.json"),
			"{\"base\":\"" + hex8(code.getStart().getOffset()) + "\",\"size\":" + size + "}\n");
		return size;
	}

	/** Up to 12 "name@address" entries, as a JSON array. */
	private static String names(Collection<Function> fns) {
		StringBuilder sb = new StringBuilder("[");
		int i = 0;
		for (Function f : fns) {
			if (i >= 12) {
				break;
			}
			sb.append(i > 0 ? "," : "").append('"').append(esc(f.getName()))
				.append('@').append(hex8(f.getEntryPoint().getOffset())).append('"');
			i++;
		}
		return sb.append("]").toString();
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private static String toHex(byte[] b) {
		StringBuilder sb = new StringBuilder();
		for (byte x : b) {
			sb.append(Character.forDigit((x >> 4) & 0xf, 16));
			sb.append(Character.forDigit(x & 0xf, 16));
		}
		return sb.toString();
	}

	private static String esc(String s) {
		return s == null ? "" : s.replace("\\", "\\\\").replace("\"", "\\\"");
	}

	private static void write(File f, String text) throws Exception {
		try (PrintWriter pw = new PrintWriter(
			new OutputStreamWriter(new FileOutputStream(f), StandardCharsets.UTF_8))) {
			pw.print(text);
		}
	}
}
