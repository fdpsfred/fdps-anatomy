// Transcribe block-triage verdicts into Ghidra.
//
// One agent read one undefined block and decided what it is; this script puts
// that decision in without making any of its own. Code blocks are disassembled
// and turned into functions, data blocks get a type and a label, alignment
// filler gets defined as bytes so that "undefined" comes to mean "nobody has
// looked at this yet".
//
// Usage: run_ghidra_script with two or more arguments:
//   <verdict dir> all           apply every verdict file in the directory
//   <verdict dir> <start> ...   apply only these blocks (start address, 8 hex)
//
// Verdict file <start>.json, written by the triage agent:
//   {
//     "start": "0003f6d4", "end": "...", "size": 3448,
//     "kind": "code" | "data" | "pad" | "mixed",
//     "pool": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
//     "entries":    [{"addr": "0003f6d4", "name": "crt_memcpy", "note": "..."}],
//     "data_items": [{"addr": "0003f6d4", "type": "byte[16]", "label": "...",
//                     "note": "..."}],
//     "comment": "one line recorded on the block",
//     "evidence": "...", "confidence": "high", "open_question": ""
//   }
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.TreeMap;
import java.util.Map;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.TerminatedStringDataType;
import ghidra.program.model.data.WordDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyBlockTriage extends GhidraScript {

	private final List<String> problems = new ArrayList<>();
	private final Map<String, Integer> kinds = new TreeMap<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyBlockTriage <verdict dir> all|<start>...");
			return;
		}
		File dir = new File(argv[0]);
		List<String> wanted = new ArrayList<>();
		if (argv.length == 2 && "all".equalsIgnoreCase(argv[1])) {
			File[] files = dir.listFiles((d, n) -> n.endsWith(".json"));
			if (files != null) {
				Arrays.sort(files);
				for (File f : files) {
					wanted.add(f.getName().replace(".json", ""));
				}
			}
		}
		else {
			wanted.addAll(Arrays.asList(argv).subList(1, argv.length));
		}

		int functionsCreated = 0;
		int dataDefined = 0;
		int padDefined = 0;
		int deadCode = 0;
		int applied = 0;
		for (String start : wanted) {
			File f = new File(dir, start + ".json");
			if (!f.isFile()) {
				problems.add("NO VERDICT FILE " + start);
				continue;
			}
			JsonObject v;
			try {
				v = JsonParser.parseString(
					new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8))
					.getAsJsonObject();
			}
			catch (Exception e) {
				problems.add("UNPARSEABLE " + start + ": " + e.getMessage());
				continue;
			}
			String kind = str(v, "kind");
			kinds.merge(kind == null ? "?" : kind, 1, Integer::sum);
			try {
				functionsCreated += applyEntries(v);
				deadCode += applyDeadCode(v, kind);
				int[] counts = applyData(v, kind);
				dataDefined += counts[0];
				padDefined += counts[1];
				applyComment(v);
				applied++;
			}
			catch (Exception e) {
				problems.add("FAILED " + start + ": " + e.getClass().getSimpleName() + " "
					+ e.getMessage());
			}
		}

		println("blocks applied = " + applied + " of " + wanted.size());
		println("functions created = " + functionsCreated);
		println("dead code ranges disassembled = " + deadCode);
		println("data items defined = " + dataDefined);
		println("pad ranges defined = " + padDefined);
		println("kinds = " + kinds);
		println("problems = " + problems.size());
		for (String p : problems) {
			println("  " + p);
		}
	}

	private int applyEntries(JsonObject v) throws Exception {
		JsonArray entries = v.getAsJsonArray("entries");
		if (entries == null) {
			return 0;
		}
		// The block agent only records a hint; the pool itself is judged per
		// function in the second stage, with the library evidence in hand.
		String hint = str(v, "pool_hint");
		int made = 0;
		for (JsonElement e : entries) {
			JsonObject o = e.getAsJsonObject();
			Address at = addr(str(o, "addr"));
			if (at == null) {
				problems.add("BAD ENTRY ADDRESS in " + str(v, "start"));
				continue;
			}
			Function existing = getFunctionAt(at);
			if (existing == null) {
				clearListing(at);
				if (!disassemble(at)) {
					problems.add("DISASSEMBLE FAILED at " + at);
					continue;
				}
				existing = createFunction(at, null);
				if (existing == null) {
					problems.add("CREATE FUNCTION FAILED at " + at);
					continue;
				}
				made++;
			}
			String name = str(o, "name");
			if (name != null && !name.isEmpty() && existing.getName().startsWith("FUN_")) {
				try {
					existing.setName(uniqueName(name, at), SourceType.USER_DEFINED);
				}
				catch (Exception ex) {
					problems.add("RENAME FAILED " + at + " -> " + name + ": " + ex.getMessage());
				}
			}
			existing.addTag("orphan_block");
			String note = str(o, "note");
			if (note != null && !note.isEmpty()) {
				setPlateComment(at, note
					+ (hint == null || hint.isEmpty() ? "" : "\nPool hint from block triage: " + hint));
			}
		}
		return made;
	}

	/**
	 * Code with no entry point of its own: an instruction Ghidra never reached,
	 * sitting inside a function body that already exists. Compiler-emitted stack
	 * cleanup after a no-return call is the usual case. Disassembling it fills
	 * the hole in the listing; creating a function there would be wrong.
	 *
	 * Only done when the bytes really are inside an existing body -- outside
	 * one, disassembling would manufacture orphan code and fail the gate, so
	 * the range is left alone and reported instead.
	 */
	private int applyDeadCode(JsonObject v, String kind) throws Exception {
		if (!"code".equals(kind) && !"mixed".equals(kind)) {
			return 0;
		}
		JsonArray entries = v.getAsJsonArray("entries");
		if (entries != null && entries.size() > 0) {
			return 0;
		}
		Address at = addr(str(v, "start"));
		int size = v.has("size") ? v.get("size").getAsInt() : 0;
		if (at == null || size <= 0) {
			return 0;
		}
		Address last = at.add(size - 1);

		// getFunctionContaining tests the body, not the address range, and the
		// body is exactly what has a hole here: Ghidra drops the fall-through at
		// a no-return call, so the cleanup instruction after it belongs to no
		// body at all. Fall back to the function whose range spans the block.
		Function owner = getFunctionContaining(at);
		boolean needsBodyRepair = false;
		if (owner == null) {
			Function before = getFunctionBefore(at);
			if (before != null) {
				Address bodyEnd = before.getBody().getMaxAddress();
				// Either a hole inside the previous function's span, or the tail
				// immediately after its body -- both are cases where flow stopped
				// early (a no-return call, an INT3, a far return) and the bytes
				// belong to the routine that ends there.
				if (bodyEnd.compareTo(last) >= 0 || bodyEnd.add(1).equals(at)) {
					owner = before;
					needsBodyRepair = true;
				}
			}
		}
		if (owner == null) {
			problems.add("DEAD CODE OUTSIDE ANY FUNCTION at " + at + ", left undefined");
			return 0;
		}
		try {
			clearListing(at, last);
			if (!disassemble(at)) {
				problems.add("DEAD CODE DISASSEMBLE FAILED at " + at);
				return 0;
			}
			if (needsBodyRepair) {
				AddressSet body = new AddressSet(owner.getBody());
				body.addRange(at, last);
				owner.setBody(body);
			}
		}
		catch (Exception e) {
			problems.add("DEAD CODE FAILED " + at + ": " + e.getClass().getSimpleName() + " "
				+ e.getMessage());
			return 0;
		}
		return 1;
	}

	/** Returns {data items defined, pad ranges defined}. */
	private int[] applyData(JsonObject v, String kind) throws Exception {
		int data = 0;
		int pad = 0;
		JsonArray items = v.getAsJsonArray("data_items");
		if (items != null) {
			for (JsonElement e : items) {
				JsonObject o = e.getAsJsonObject();
				Address at = addr(str(o, "addr"));
				DataType dt = parseType(str(o, "type"));
				if (at == null || dt == null) {
					problems.add("BAD DATA ITEM in " + str(v, "start") + ": " + o);
					continue;
				}
				try {
					clearListing(at, at.add(Math.max(dt.getLength(), 1) - 1));
					createData(at, dt);
					String label = str(o, "label");
					if (label != null && !label.isEmpty()) {
						createLabel(at, label, true, SourceType.USER_DEFINED);
					}
					String note = str(o, "note");
					if (note != null && !note.isEmpty()) {
						setEOLComment(at, note);
					}
					data++;
				}
				catch (Exception ex) {
					problems.add("DATA FAILED " + at + ": " + ex.getMessage());
				}
			}
		}
		// Alignment filler: define it as bytes so that "undefined" keeps meaning
		// "not yet judged" once the sweep is over.
		if ("pad".equals(kind)) {
			Address at = addr(str(v, "start"));
			int size = v.has("size") ? v.get("size").getAsInt() : 0;
			if (at != null && size > 0) {
				try {
					clearListing(at, at.add(size - 1));
					createData(at, new ArrayDataType(ByteDataType.dataType, size, 1));
					pad++;
				}
				catch (Exception ex) {
					problems.add("PAD FAILED " + at + ": " + ex.getMessage());
				}
			}
		}
		return new int[] { data, pad };
	}

	private void applyComment(JsonObject v) throws Exception {
		String comment = str(v, "comment");
		Address at = addr(str(v, "start"));
		if (comment == null || comment.isEmpty() || at == null) {
			return;
		}
		if (getFunctionAt(at) != null) {
			return;  // the entry note already went in as a plate comment
		}
		setEOLComment(at, comment);
	}

	private String uniqueName(String name, Address at) {
		if (getGlobalFunctions(name).isEmpty()) {
			return name;
		}
		problems.add("NAME TAKEN " + name + ", using " + name + "_" + at);
		return name + "_" + at;
	}

	private DataType parseType(String spec) {
		if (spec == null) {
			return null;
		}
		spec = spec.trim().toLowerCase();
		if (spec.equals("string")) {
			return TerminatedStringDataType.dataType;
		}
		int lb = spec.indexOf('[');
		int count = 1;
		String base = spec;
		if (lb > 0 && spec.endsWith("]")) {
			base = spec.substring(0, lb);
			try {
				count = Integer.parseInt(spec.substring(lb + 1, spec.length() - 1));
			}
			catch (NumberFormatException e) {
				return null;
			}
		}
		DataType element;
		switch (base) {
			case "byte": case "db": case "undefined1": element = ByteDataType.dataType; break;
			case "word": case "dw": element = WordDataType.dataType; break;
			case "dword": case "dd": element = DWordDataType.dataType; break;
			case "ptr": case "pointer": element = PointerDataType.dataType; break;
			default: return null;
		}
		if (count <= 1 && lb < 0) {
			return element;
		}
		return new ArrayDataType(element, count, element.getLength());
	}

	private Address addr(String hex) {
		if (hex == null || hex.isEmpty()) {
			return null;
		}
		try {
			return toAddr(Long.parseLong(hex.replace("0x", "").trim(), 16));
		}
		catch (Exception e) {
			return null;
		}
	}

	private String str(JsonObject o, String key) {
		JsonElement e = o == null ? null : o.get(key);
		return e == null || e.isJsonNull() ? null : e.getAsString();
	}
}
