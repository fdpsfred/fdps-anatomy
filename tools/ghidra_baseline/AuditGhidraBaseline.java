// Audit the Ghidra baseline of FDPS.LE: memory block attributes against the LE object
// table, orphan code (instructions outside every function body), undefined bytes inside
// the code object, and error bookmarks.
//
// Read-only: the script never writes to the program database.
//
// Usage (Ghidra MCP): run_ghidra_script with this absolute path. Optional argument: the
// output directory (default workspace/ghidra_baseline).
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.listing.BookmarkManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.ReferenceManager;

public class AuditGhidraBaseline extends GhidraScript {

	private static final String EXPECTED_PROGRAM = "FDPS.LE";
	private static final String DEFAULT_OUT_DIR =
		"C:/Users/fdpsf/Documents/fdps-anatomy/workspace/ghidra_baseline";
	private static final String CODE_BLOCK = ".object1";

	@Override
	public void run() throws Exception {
		if (currentProgram == null || !EXPECTED_PROGRAM.equals(currentProgram.getName())) {
			throw new IllegalStateException("current program is " +
				(currentProgram == null ? "(none)" : currentProgram.getName()) +
				", expected " + EXPECTED_PROGRAM);
		}

		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : DEFAULT_OUT_DIR);

		StringBuilder report = new StringBuilder();
		reportMemoryBlocks(report);
		reportLeObjectTable(report);
		int orphanRanges = reportOrphanCode(report);
		long undefinedBytes = reportUndefinedBytes(report);
		int errorBookmarks = reportErrorBookmarks(report);
		reportFunctionSummary(report);

		report.append("\n# Gate\n");
		report.append("orphan code ranges = ").append(orphanRanges).append(" (target 0)\n");
		report.append("error bookmarks = ").append(errorBookmarks).append(" (target 0)\n");
		report.append("undefined bytes in ").append(CODE_BLOCK).append(" = ")
			.append(undefinedBytes).append(" (target 0)\n");

		outDir.mkdirs();
		File out = new File(outDir, "baseline_audit.txt");
		try (PrintWriter pw = new PrintWriter(
			new OutputStreamWriter(new FileOutputStream(out), StandardCharsets.UTF_8))) {
			pw.print(report.toString());
		}
		println(report.toString());
		println("audit written to " + out.getAbsolutePath());
	}

	private void reportMemoryBlocks(StringBuilder sb) {
		sb.append("# Memory blocks\n");
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			sb.append(b.getName())
				.append(" ").append(b.getStart()).append("-").append(b.getEnd())
				.append(" ").append(b.isRead() ? "r" : "-")
				.append(b.isWrite() ? "w" : "-")
				.append(b.isExecute() ? "x" : "-")
				.append(" init=").append(b.isInitialized())
				.append(" size=0x").append(Long.toHexString(b.getSize()))
				.append("\n");
		}
	}

	// The LE object table is the authority for permissions and for the initialized/BSS
	// split: an object's page count covers only the file-backed part of its virtual size.
	private void reportLeObjectTable(StringBuilder sb) throws Exception {
		sb.append("\n# LE object table (authority)\n");
		Memory mem = currentProgram.getMemory();
		Address img = currentProgram.getAddressFactory().getAddressSpace(".image").getAddress(0);
		byte[] hdr = new byte[0x40];
		mem.getBytes(img, hdr);
		int lfanew = readInt(hdr, 0x3c);
		byte[] le = new byte[0xc0];
		mem.getBytes(img.add(lfanew), le);
		if (le[0] != 'L' || le[1] != 'E') {
			sb.append("no LE signature at e_lfanew=0x").append(Integer.toHexString(lfanew)).append("\n");
			return;
		}
		int pageSize = readInt(le, 0x28);
		int objectCount = readInt(le, 0x44);
		int objectTableOffset = readInt(le, 0x40);
		byte[] ot = new byte[objectCount * 24];
		mem.getBytes(img.add(lfanew + objectTableOffset), ot);
		sb.append("page size = 0x").append(Integer.toHexString(pageSize))
			.append(", objects = ").append(objectCount).append("\n");
		for (int i = 0; i < objectCount; i++) {
			int base = i * 24;
			long vsize = readInt(ot, base) & 0xffffffffL;
			long reloc = readInt(ot, base + 4) & 0xffffffffL;
			int flags = readInt(ot, base + 8);
			int pageCount = readInt(ot, base + 16);
			long fileBacked = (long) pageCount * pageSize;
			sb.append("object ").append(i + 1)
				.append(": base=0x").append(Long.toHexString(reloc))
				.append(" vsize=0x").append(Long.toHexString(vsize))
				.append(" flags=0x").append(Integer.toHexString(flags))
				.append(" [").append((flags & 1) != 0 ? "R" : "-")
				.append((flags & 2) != 0 ? "W" : "-")
				.append((flags & 4) != 0 ? "X" : "-").append("]")
				.append(" pages=").append(pageCount)
				.append(" fileBacked=0x").append(Long.toHexString(fileBacked));
			if (fileBacked < vsize) {
				sb.append(" bss=0x").append(Long.toHexString(reloc + fileBacked))
					.append("-0x").append(Long.toHexString(reloc + vsize - 1));
			}
			sb.append("\n");
		}
	}

	private int readInt(byte[] a, int off) {
		return (a[off] & 0xff) | ((a[off + 1] & 0xff) << 8) |
			((a[off + 2] & 0xff) << 16) | ((a[off + 3] & 0xff) << 24);
	}

	// Instructions that belong to no function body. A clean baseline has none: every
	// instruction is either a function entry, inside a body, or reached through a jump
	// table whose references have been attached to the dispatching instruction.
	private int reportOrphanCode(StringBuilder sb) {
		sb.append("\n# Orphan code (instructions outside every function body)\n");
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		AddressSet orphan = new AddressSet();
		InstructionIterator it = listing.getInstructions(true);
		while (it.hasNext()) {
			Instruction in = it.next();
			if (fm.getFunctionContaining(in.getMinAddress()) == null) {
				orphan.addRange(in.getMinAddress(), in.getMaxAddress());
			}
		}
		int count = 0;
		long bytes = 0;
		AddressRangeIterator ri = orphan.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			count++;
			bytes += r.getLength();
			sb.append("  ").append(r.getMinAddress()).append("-").append(r.getMaxAddress())
				.append(" size=").append(r.getLength()).append("\n");
		}
		sb.append("ranges = ").append(count).append(", bytes = ").append(bytes).append("\n");
		return count;
	}

	// Undefined bytes inside the code object, split into inter-function alignment padding,
	// referenced data, and unreferenced blocks (linked-in but never called library code).
	private long reportUndefinedBytes(StringBuilder sb) throws Exception {
		sb.append("\n# Undefined bytes in ").append(CODE_BLOCK).append("\n");
		Memory mem = currentProgram.getMemory();
		MemoryBlock code = mem.getBlock(CODE_BLOCK);
		if (code == null) {
			sb.append("no such block\n");
			return 0;
		}
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
		Map<String, long[]> tally = new TreeMap<>();
		List<String> notable = new ArrayList<>();
		long totalUndefined = 0;
		AddressRangeIterator ri = undefined.getAddressRanges();
		while (ri.hasNext()) {
			AddressRange r = ri.next();
			long len = r.getLength();
			totalUndefined += len;
			byte[] b = new byte[(int) Math.min(len, 4096)];
			mem.getBytes(r.getMinAddress(), b);
			boolean referenced =
				rm.getReferenceDestinationIterator(
					new AddressSet(r.getMinAddress(), r.getMaxAddress()), true).hasNext();
			String kind = (len <= 16 && isAlignmentFiller(b)) ? "pad"
				: referenced ? "data_referenced" : "unreferenced";
			long[] t = tally.computeIfAbsent(kind, k -> new long[2]);
			t[0]++;
			t[1] += len;
			if (!"pad".equals(kind) && len >= 64) {
				notable.add(kind + " " + r.getMinAddress() + "-" + r.getMaxAddress() + " size=" + len);
			}
		}
		for (Map.Entry<String, long[]> e : tally.entrySet()) {
			sb.append(e.getKey()).append(": ranges=").append(e.getValue()[0])
				.append(" bytes=").append(e.getValue()[1]).append("\n");
		}
		sb.append("ranges >= 64 bytes:\n");
		for (String s : notable) {
			sb.append("  ").append(s).append("\n");
		}
		return totalUndefined;
	}

	// Watcom pads between functions with multi-byte NOP forms built from lea/mov encodings
	// plus 0x90 and 0xcc, so a short run made only of those bytes is alignment filler.
	private boolean isAlignmentFiller(byte[] b) {
		for (byte x : b) {
			int v = x & 0xff;
			switch (v) {
				case 0x00: case 0x20: case 0x22: case 0x24: case 0x36: case 0x40:
				case 0x44: case 0x52: case 0x54: case 0x8b: case 0x8d: case 0x90:
				case 0x92: case 0xc0: case 0xcc: case 0xd2:
					break;
				default:
					return false;
			}
		}
		return true;
	}

	private int reportErrorBookmarks(StringBuilder sb) {
		sb.append("\n# Error bookmarks\n");
		BookmarkManager bm = currentProgram.getBookmarkManager();
		Iterator<Bookmark> it = bm.getBookmarksIterator("Error");
		int count = 0;
		while (it.hasNext()) {
			Bookmark b = it.next();
			count++;
			sb.append("  ").append(b.getAddress()).append(" ").append(b.getCategory())
				.append(": ").append(b.getComment()).append("\n");
		}
		sb.append("count = ").append(count).append("\n");
		return count;
	}

	private void reportFunctionSummary(StringBuilder sb) {
		sb.append("\n# Functions\n");
		FunctionManager fm = currentProgram.getFunctionManager();
		Map<String, Integer> conventions = new TreeMap<>();
		Map<String, Integer> sources = new TreeMap<>();
		int defaultNamed = 0;
		FunctionIterator it = fm.getFunctions(true);
		while (it.hasNext()) {
			Function f = it.next();
			conventions.merge(String.valueOf(f.getCallingConventionName()), 1, Integer::sum);
			sources.merge(String.valueOf(f.getSignatureSource()), 1, Integer::sum);
			if (f.getName().startsWith("FUN_")) {
				defaultNamed++;
			}
		}
		sb.append("count = ").append(fm.getFunctionCount()).append("\n");
		sb.append("still named FUN_* = ").append(defaultNamed).append("\n");
		sb.append("calling conventions = ").append(conventions).append("\n");
		sb.append("signature sources = ").append(sources).append("\n");
	}
}
