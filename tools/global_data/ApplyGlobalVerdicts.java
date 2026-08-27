// Transcribe one round of ticket 17 global-data verdicts into Ghidra.
//
// Each verdict came from an agent that read one anchor's bytes and every
// instruction that touches it, and decided four things: which pool the global
// belongs to, whether it is a variable of its own or the interior of one that
// starts lower down, what it should be called, and what type it has. This
// script writes those down. It judges nothing: a verdict it cannot apply is
// reported and left alone (ADR-0007 5.3).
//
// Two things it deliberately refuses to do:
//
//   * It will not invent a boundary. A verdict whose type is bigger than the
//     bytes to the next anchor that another verdict claims as its own is
//     reported as an OVERLAP and its type is not applied. Two agents
//     contradicting each other about where a variable ends is exactly the
//     situation that must reach a human-readable report rather than be silently
//     resolved by whichever one ran second.
//   * It will not rename an interior. An anchor the agent judged to be inside a
//     larger object keeps Ghidra's default label; the owning object's type is
//     what gives those bytes a name, as a field or an array element.
//
// Usage: run_ghidra_script with two or more arguments:
//   <verdict dir> all           apply every verdict file in the directory
//   <verdict dir> <addr> ...    apply only these anchors (8 hex digits)
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyGlobalVerdicts extends GhidraScript {

	private final List<String> problems = new ArrayList<>();
	private final List<String> overlaps = new ArrayList<>();
	private final List<String> conflicts = new ArrayList<>();
	private final List<String> segmentIssues = new ArrayList<>();

	private int applied;
	private int renamed;
	private int retyped;
	private int commented;
	private int interiors;
	/** Names withdrawn because the address turned out to be an interior. */
	private int unnamed;

	/** addr -> verdict, for the whole round, so overlaps can be seen. */
	private final Map<Long, JsonObject> round = new TreeMap<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyGlobalVerdicts <verdict dir> all|<addr>...");
			return;
		}
		File dir = new File(argv[0]);
		List<String> wanted = new ArrayList<>();
		if (argv.length == 2 && "all".equalsIgnoreCase(argv[1])) {
			File[] files = dir.listFiles((d, n) -> n.endsWith(".json"));
			if (files != null) {
				Arrays.sort(files);
				for (File f : files) {
					wanted.add(f.getName().substring(0, f.getName().length() - 5));
				}
			}
		}
		else {
			wanted.addAll(Arrays.asList(argv).subList(1, argv.length));
		}

		// Read everything first: an overlap is a relation between two verdicts,
		// so it cannot be seen while applying them one at a time.
		for (String key : wanted) {
			JsonObject v = read(dir, key);
			if (v != null) {
				round.put(Long.parseLong(key, 16), v);
			}
		}

		for (Map.Entry<Long, JsonObject> e : round.entrySet()) {
			applyOne(e.getKey(), e.getValue());
		}

		println("verdicts applied = " + applied + " of " + wanted.size());
		println("renamed = " + renamed);
		println("types applied = " + retyped);
		println("comments written = " + commented);
		println("interiors left to their owner = " + interiors);
		println("stale names withdrawn from interiors = " + unnamed);
		println("overlaps = " + overlaps.size());
		for (String s : overlaps) {
			println("  OVERLAP " + s);
		}
		println("name conflicts = " + conflicts.size());
		for (String s : conflicts) {
			println("  NAME TAKEN " + s);
		}
		println("segment issues = " + segmentIssues.size());
		for (String s : segmentIssues) {
			println("  SEGMENT " + s);
		}
		println("problems = " + problems.size());
		for (String s : problems) {
			println("  " + s);
		}
	}

	private JsonObject read(File dir, String key) {
		File f = new File(dir, key + ".json");
		if (!f.isFile()) {
			problems.add("NO VERDICT FILE " + key);
			return null;
		}
		try {
			return JsonParser.parseString(
				new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8)).getAsJsonObject();
		}
		catch (Exception e) {
			problems.add("UNPARSEABLE " + key + ": " + e.getMessage());
			return null;
		}
	}

	private void applyOne(long off, JsonObject v) {
		String key = String.format("%08x", off);
		Address a = toAddr(off);
		if (a == null || getMemoryBlock(a) == null) {
			problems.add("NOT IN MEMORY " + key);
			return;
		}

		String seg = str(v, "segment_check");
		if (!seg.isEmpty()) {
			segmentIssues.add(key + ": " + seg);
		}

		String classification = str(v, "classification");
		if ("interior".equals(classification) || "padding".equals(classification)) {
			// Nothing to name. Record who owns it, so the next reader of the
			// database does not have to re-derive it.
			String owner = str(v, "belongs_to");
			String note = "padding".equals(classification)
				? "alignment padding"
				: "interior of the object at " + (owner.isEmpty() ? "(unstated)" : owner);
			try {
				setEOLComment(a, note);
			}
			catch (Exception e) {
				problems.add("COMMENT FAILED " + key + ": " + e.getMessage());
			}
			// An address judged an interior may have been judged a variable in an
			// earlier round and given a name then -- that is exactly what happens
			// when a struct lands over it and the rescan re-reads it. The verdict
			// now says these bytes are not a symbol of their own, so the name has
			// to go: leaving it strands a label inside another object, which the
			// audit rejects and a reader would take for a second variable.
			Symbol stale = getSymbolAt(a);
			if (stale != null && stale.getSource() == SourceType.USER_DEFINED) {
				try {
					stale.delete();
					unnamed++;
				}
				catch (Exception e) {
					problems.add("UNNAME FAILED " + key + " (" + stale.getName() + "): "
						+ e.getMessage());
				}
			}
			interiors++;
			applied++;
			return;
		}

		boolean ok = true;

		// ---- name ----------------------------------------------------------
		String name = "";
		JsonObject nameObj = obj(v, "name");
		if (nameObj != null) {
			name = str(nameObj, "verdict");
		}
		if (!name.isEmpty()) {
			SymbolTable st = currentProgram.getSymbolTable();
			List<Symbol> holders = st.getGlobalSymbols(name);
			boolean heldElsewhere = false;
			for (Symbol s : holders) {
				if (!s.getAddress().equals(a) && s.getSource() != SourceType.DEFAULT) {
					conflicts.add(key + " wanted " + name + ", held by "
						+ String.format("%08x", s.getAddress().getOffset()));
					heldElsewhere = true;
					break;
				}
			}
			if (heldElsewhere) {
				ok = false;
			}
			else {
				try {
					Symbol existing = getSymbolAt(a);
					if (existing != null && existing.getSource() != SourceType.DEFAULT) {
						existing.setName(name, SourceType.USER_DEFINED);
					}
					else {
						createLabel(a, name, true, SourceType.USER_DEFINED);
					}
					renamed++;
				}
				catch (Exception e) {
					problems.add("RENAME FAILED " + key + " -> " + name + ": " + e.getMessage());
					ok = false;
				}
			}
		}

		// ---- type ----------------------------------------------------------
		JsonObject typeObj = obj(v, "type");
		String typeName = typeObj == null ? "" : str(typeObj, "verdict");
		if (!typeName.isEmpty()) {
			DataType dt = resolveType(typeName);
			if (dt == null) {
				problems.add("UNKNOWN TYPE " + key + ": " + typeName);
				ok = false;
			}
			else if (dt.getLength() <= 0) {
				problems.add("ZERO LENGTH TYPE " + key + ": " + typeName);
				ok = false;
			}
			else {
				long end = off + dt.getLength();
				Long clash = firstClaimedAnchorIn(off, end);
				if (clash != null) {
					overlaps.add(key + " as " + typeName + " (" + dt.getLength()
						+ " bytes) would swallow " + String.format("%08x", clash)
						+ ", which claims to be a variable of its own");
					ok = false;
				}
				else {
					try {
						clearListing(a, toAddr(end - 1));
						createData(a, dt);
						retyped++;
					}
					catch (Exception e) {
						problems.add("TYPE FAILED " + key + " as " + typeName + ": " + e.getMessage());
						ok = false;
					}
				}
			}
		}

		// ---- comment -------------------------------------------------------
		JsonObject commentObj = obj(v, "comment");
		String text = commentObj == null ? "" : str(commentObj, "text");
		if (!text.isEmpty()) {
			try {
				setPlateComment(a, text);
				commented++;
			}
			catch (Exception e) {
				problems.add("COMMENT FAILED " + key + ": " + e.getMessage());
				ok = false;
			}
		}

		if (ok) {
			applied++;
		}
	}

	/**
	 * The first anchor strictly inside (off, end) that some verdict in this
	 * round claims is a variable in its own right. Interiors do not count --
	 * being swallowed is what they said they were for.
	 */
	private Long firstClaimedAnchorIn(long off, long end) {
		for (Map.Entry<Long, JsonObject> e : round.entrySet()) {
			long other = e.getKey();
			if (other <= off || other >= end) {
				continue;
			}
			String c = str(e.getValue(), "classification");
			if (!"interior".equals(c) && !"padding".equals(c)) {
				return other;
			}
		}
		return null;
	}

	/**
	 * Resolve a type the way an agent would write it: a name, optionally
	 * followed by pointer stars and one array dimension. Anything more elaborate
	 * is reported rather than guessed at.
	 */
	private DataType resolveType(String spec) {
		String s = spec.trim();
		int count = -1;
		int bracket = s.indexOf('[');
		if (bracket >= 0) {
			if (!s.endsWith("]")) {
				return null;
			}
			try {
				count = Integer.parseInt(s.substring(bracket + 1, s.length() - 1).trim());
			}
			catch (NumberFormatException e) {
				return null;
			}
			s = s.substring(0, bracket).trim();
		}
		int stars = 0;
		while (s.endsWith("*")) {
			stars++;
			s = s.substring(0, s.length() - 1).trim();
		}
		DataType base = lookup(s);
		if (base == null) {
			return null;
		}
		for (int i = 0; i < stars; i++) {
			base = new PointerDataType(base);
		}
		if (count >= 0) {
			if (count == 0 || base.getLength() <= 0) {
				return null;
			}
			base = new ArrayDataType(base, count, base.getLength());
		}
		return base;
	}

	private DataType lookup(String name) {
		if ("func_ptr".equals(name)) {
			return funcPtr();
		}
		DataType local = currentProgram.getDataTypeManager().getDataType("/" + name);
		if (local != null) {
			return local;
		}
		DataType[] found = getDataTypes(name);
		return found != null && found.length > 0 ? found[0] : null;
	}

	/**
	 * A pointer to code, for the dispatch tables. This binary has at least four
	 * of them -- the chapter and event handler tables at 0x60074, 0x601c4,
	 * 0x6028c and 0x60304 -- and every function in them is reached only through
	 * its table, never by a direct CALL, so typing the table as a plain dword
	 * array is how they stay invisible.
	 *
	 * The signature is a placeholder: void (void). What each table's entries
	 * actually take is a per-function question and it belongs to the emit
	 * tickets, not here. What matters at this stage is that the slots are code
	 * pointers, which is what makes Ghidra follow them.
	 */
	private DataType funcPtr() {
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		CategoryPath cat = new CategoryPath("/fdps");
		DataType existing = dtm.getDataType(cat, "void_fn");
		if (existing == null) {
			FunctionDefinitionDataType fn = new FunctionDefinitionDataType(cat, "void_fn", dtm);
			fn.setReturnType(VoidDataType.dataType);
			existing = dtm.addDataType(fn, DataTypeConflictHandler.KEEP_HANDLER);
		}
		return new PointerDataType(existing);
	}

	private static String str(JsonObject o, String key) {
		if (o == null) {
			return "";
		}
		JsonElement e = o.get(key);
		return e == null || e.isJsonNull() ? "" : e.getAsString();
	}

	private static JsonObject obj(JsonObject o, String key) {
		JsonElement e = o.get(key);
		return e != null && e.isJsonObject() ? e.getAsJsonObject() : null;
	}
}
