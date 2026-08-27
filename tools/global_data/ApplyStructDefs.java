// Transcribe one round of ticket 17 struct definitions into Ghidra.
//
// A struct is not a per-item job the way a global symbol is. What a field means
// comes from every site that touches it, so one agent studies one whole struct
// and writes one file describing the layout; this script puts that layout in the
// data type manager and applies it where the verdict says it belongs. It judges
// nothing (ADR-0007 5.3): a field that will not fit, a variable that is not
// there, a type that does not resolve -- all reported, none worked around.
//
// Two rules it enforces because they are cheap here and expensive later:
//
//   * A field must fit inside the declared size and must not overlap one
//     already placed. Ghidra would happily grow the struct instead; a struct
//     that silently grew past the stride its callers index by is a layout that
//     looks applied and is wrong.
//   * A variable named in apply_to must exist under that name in that function.
//     Ticket 15 gave those variables their names, so a miss means the verdict is
//     describing a function that has since changed, and that is worth a line in
//     the report rather than a silent skip.
//
// Usage: run_ghidra_script with two or more arguments:
//   <struct dir> all            apply every struct file in the directory
//   <struct dir> <name> ...     apply only these structs (file basenames)
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.google.gson.JsonArray;
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
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.symbol.SourceType;

public class ApplyStructDefs extends GhidraScript {

	private static final CategoryPath CATEGORY = new CategoryPath("/fdps");

	/** The layout itself is wrong about the program. These are gate failures. */
	private final List<String> problems = new ArrayList<>();
	/**
	 * A place the layout said to apply the type could not take it. Reported, and
	 * emphatically not a gate failure: the struct is in the database and
	 * correct, one call site simply reads no better than it did before. Stopping
	 * a run over it wastes everything after it, which is what the first struct
	 * round of this ticket did.
	 */
	private final List<String> targetProblems = new ArrayList<>();
	/** A field waiting on a struct nobody has laid out yet. Neither error nor done. */
	private final List<String> deferred = new ArrayList<>();
	/** Where the layout files live, for telling "not yet written" from "wrong". */
	private File structDir;
	private int structsApplied;
	private int fieldsPlaced;
	private int globalsTyped;
	private int variablesTyped;

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyStructDefs <struct dir> all|<name>...");
			return;
		}
		File dir = new File(argv[0]);
		structDir = dir;
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

		// Every struct is defined before anything is applied, so that a struct
		// whose field is a pointer to another struct resolves either way round.
		List<JsonObject> defs = new ArrayList<>();
		for (String key : wanted) {
			JsonObject v = read(dir, key);
			if (v != null) {
				defs.add(v);
			}
		}
		// Three passes, and the first one exists for a reason worth stating: a
		// linked list's next pointer is of the type being defined, so a struct
		// has to be resolvable by name before its own fields are placed. The
		// same pass makes a field of a sibling struct in the round resolve
		// whichever order the files happen to be read in.
		for (JsonObject v : defs) {
			declare(v);
		}
		for (JsonObject v : defs) {
			fill(v);
		}
		// Where two layouts claim the same bytes, neither is applied. Doing this
		// before any of them is written is the whole point: applying them in
		// turn does not produce a merge, it produces whichever one ran last,
		// because clearing any byte of a Data object in Ghidra removes all of
		// it. That is how a 122-byte emu387 record vanished on this ticket while
		// the script reported it as successfully typed.
		planGlobalTargets(defs);
		for (JsonObject v : defs) {
			applyWhere(v);
		}

		println("structs applied = " + structsApplied + " of " + wanted.size());
		println("fields placed = " + fieldsPlaced);
		println("globals typed = " + globalsTyped);
		println("variables typed = " + variablesTyped);
		println("deferred = " + deferred.size()
			+ " (field waiting on a struct nobody has laid out yet)");
		for (String d : deferred) {
			println("  DEFERRED " + d);
		}
		println("problems = " + problems.size());
		for (String p : problems) {
			println("  " + p);
		}
		println("target problems = " + targetProblems.size());
		for (String p : targetProblems) {
			println("  TARGET " + p);
		}
	}

	private JsonObject read(File dir, String key) {
		File f = new File(dir, key + ".json");
		if (!f.isFile()) {
			problems.add("NO STRUCT FILE " + key);
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

	/** True when the layout file concluded there is nothing to define. */
	private boolean absent(JsonObject v) {
		return v.has("exists_in_image") && !v.get("exists_in_image").getAsBoolean();
	}

	/**
	 * Put an empty struct of the right size in the data type manager, so that
	 * the name resolves while the fields are being placed.
	 */
	private void declare(JsonObject v) {
		String name = str(v, "name");
		if (name.isEmpty() || absent(v)) {
			return;
		}
		int size = num(v, "size");
		if (size <= 0) {
			return;   // fill() reports it; declaring a zero-length shell helps nobody
		}
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		if (dtm.getDataType(CATEGORY, name) != null) {
			return;
		}
		try {
			dtm.addDataType(new StructureDataType(CATEGORY, name, size, dtm),
				DataTypeConflictHandler.KEEP_HANDLER);
		}
		catch (Exception e) {
			problems.add("STRUCT DECLARE " + name + ": " + e.getMessage());
		}
	}

	/** Place the fields and store the finished struct. */
	private void fill(JsonObject v) {
		String name = str(v, "name");
		if (name.isEmpty()) {
			problems.add("STRUCT WITH NO NAME");
			return;
		}
		// A layout file can conclude that there is nothing to lay out: the
		// record is another candidate's under a second spelling (alias_of), or
		// the game reads that table out of a resource file and the executable
		// has no such structure. Both are real answers and neither wants a type
		// in the database -- defining one anyway leaves an empty shell wearing
		// the name the file just concluded was wrong, which is exactly what
		// happened to crt_file and crt_iobuf on this ticket's first struct
		// round.
		if (absent(v)) {
			String why = str(v, "alias_of");
			println("  " + name + ": no type defined ("
				+ (why.isEmpty() ? "not present in the executable" : "alias of " + why) + ")");
			return;
		}
		int size = num(v, "size");
		if (size <= 0) {
			problems.add("BAD SIZE " + name + ": " + size);
			return;
		}
		StructureDataType st = new StructureDataType(CATEGORY, name, size,
			currentProgram.getDataTypeManager());
		String desc = str(v, "description");
		if (!desc.isEmpty()) {
			st.setDescription(desc);
		}

		JsonArray fields = arr(v, "fields");
		boolean ok = true;
		if (fields != null) {
			for (JsonElement fe : fields) {
				JsonObject f = fe.getAsJsonObject();
				String fname = str(f, "name");
				int off = num(f, "offset");
				String tspec = str(f, "type");
				DataType dt = resolveType(tspec);
				if (dt == null || dt.getLength() <= 0) {
					// A field whose type is another struct that has not been laid
					// out yet is not a layout that is wrong -- it is a layout
					// waiting for its turn. fdps_save_slot holds an array of
					// fdps_unit_record, and whichever of the two is judged first
					// will name the other before it exists. Stopping the run over
					// that would make the pair unresolvable in either order.
					if (awaitingLayout(tspec)) {
						deferred.add(name + "." + fname + " needs " + tspec
							+ ", which has no layout file yet");
					}
					else {
						problems.add("FIELD TYPE " + name + "." + fname
							+ ": cannot resolve " + tspec);
						ok = false;
					}
					continue;
				}
				if (off < 0 || off + dt.getLength() > size) {
					problems.add("FIELD OUT OF BOUNDS " + name + "." + fname + " at 0x"
						+ Integer.toHexString(off) + " needs " + dt.getLength()
						+ " byte(s) of a " + size + "-byte struct");
					ok = false;
					continue;
				}
				try {
					st.replaceAtOffset(off, dt, dt.getLength(), fname, str(f, "comment"));
					fieldsPlaced++;
				}
				catch (Exception e) {
					problems.add("FIELD FAILED " + name + "." + fname + ": " + e.getMessage());
					ok = false;
				}
			}
		}

		DataType stored;
		try {
			stored = currentProgram.getDataTypeManager()
				.addDataType(st, DataTypeConflictHandler.REPLACE_HANDLER);
		}
		catch (Exception e) {
			problems.add("STRUCT FAILED " + name + ": " + e.getMessage());
			return;
		}
		if (stored.getLength() != size) {
			problems.add("STRUCT GREW " + name + ": declared " + size
				+ ", data type manager holds " + stored.getLength());
			return;
		}
		if (ok) {
			structsApplied++;
		}
	}

	/** Type the globals and variables the verdict says hold this struct. */
	/** A global address some layout in this round wants to type. */
	private static final class GlobalTarget {
		String struct;
		long start;
		long end;
	}

	/** Addresses this round must not write, keyed by "<struct>@<addr>". */
	private final java.util.Set<String> blockedTargets = new java.util.HashSet<>();

	/**
	 * Work out, before anything is written, which global targets collide.
	 *
	 * Two layouts claiming overlapping bytes is not a merge problem to be
	 * resolved by ordering; it is two agents disagreeing about what lives at an
	 * address, which is a judgement neither this script nor the order of the
	 * command line is entitled to make (ADR-0007 5.3). Both sides are blocked
	 * and both are reported.
	 */
	private void planGlobalTargets(List<JsonObject> defs) {
		List<GlobalTarget> targets = new ArrayList<>();
		for (JsonObject v : defs) {
			if (absent(v)) {
				continue;
			}
			String name = str(v, "name");
			JsonArray list = arr(v, "apply_to");
			if (list == null) {
				continue;
			}
			for (JsonElement te : list) {
				JsonObject t = te.getAsJsonObject();
				if (!"global".equals(str(t, "kind"))) {
					continue;
				}
				DataType dt = resolveType(str(t, "type"));
				if (dt == null || dt.getLength() <= 0) {
					continue;   // applyWhere reports the unresolvable type
				}
				try {
					GlobalTarget g = new GlobalTarget();
					g.struct = name;
					g.start = toAddr(str(t, "addr")).getOffset();
					g.end = g.start + dt.getLength();
					targets.add(g);
				}
				catch (Exception e) {
					// applyWhere reports the bad address
				}
			}
		}
		for (int i = 0; i < targets.size(); i++) {
			for (int j = i + 1; j < targets.size(); j++) {
				GlobalTarget a = targets.get(i);
				GlobalTarget b = targets.get(j);
				if (a.struct.equals(b.struct) || a.start >= b.end || b.start >= a.end) {
					continue;
				}
				problems.add(String.format(
					"TARGET COLLISION %s wants %08x..%08x and %s wants %08x..%08x; "
						+ "the two layouts disagree about what lives there, so neither was applied",
					a.struct, a.start, a.end - 1, b.struct, b.start, b.end - 1));
				blockedTargets.add(a.struct + "@" + String.format("%08x", a.start));
				blockedTargets.add(b.struct + "@" + String.format("%08x", b.start));
			}
		}
	}

	private void applyWhere(JsonObject v) {
		if (absent(v)) {
			return;
		}
		String name = str(v, "name");
		JsonArray targets = arr(v, "apply_to");
		if (targets == null) {
			return;
		}
		for (JsonElement te : targets) {
			JsonObject t = te.getAsJsonObject();
			String kind = str(t, "kind");
			String tspec = str(t, "type");
			DataType dt = resolveType(tspec);
			if (dt == null) {
				problems.add("APPLY TYPE " + name + ": cannot resolve " + tspec);
				continue;
			}
			if ("global".equals(kind)) {
				String addr = str(t, "addr");
				try {
					Address a = toAddr(addr);
					if (blockedTargets.contains(name + "@" + String.format("%08x", a.getOffset()))) {
						continue;   // planGlobalTargets already reported why
					}
					int len = Math.max(1, dt.getLength());
					// A defined object that starts below this address and runs
					// past it belongs to somebody else -- probably a struct an
					// earlier round applied. Clearing here would delete all of
					// it, silently, and report success.
					ghidra.program.model.listing.Data sitting = getDataContaining(a);
					if (sitting != null && !sitting.getAddress().equals(a)
							&& sitting.getAddress().add(sitting.getLength() - 1)
								.compareTo(a.add(len - 1)) >= 0) {
						problems.add(String.format(
							"TARGET COLLISION %s at %08x sits inside the %s already defined at %08x; "
								+ "typing it would delete that object entirely, so nothing was written",
							name, a.getOffset(), sitting.getDataType().getName(),
							sitting.getAddress().getOffset()));
						continue;
					}
					clearListing(a, a.add(len - 1));
					createData(a, dt);
					globalsTyped++;
				}
				catch (Exception e) {
					targetProblems.add("APPLY GLOBAL " + name + " at " + addr + ": " + e.getMessage());
				}
			}
			else if ("variable".equals(kind)) {
				applyToVariable(name, t, dt);
			}
			else {
				targetProblems.add("APPLY KIND " + name + ": unknown kind " + kind);
			}
		}
	}

	/**
	 * Give one local variable or parameter the struct's type.
	 *
	 * The interesting case is the storage conflict. A local the decompiler
	 * inferred as four bytes of stack becomes twenty-eight when it is given the
	 * struct it really holds, and the slots the decompiler invented for the rest
	 * of those bytes are then in the way. Those neighbours are not findings --
	 * they are the decompiler's guess at the same object, made before anyone
	 * knew it was one object -- so this clears them and tries again, and says in
	 * the report which ones it cleared. What it will not do is give up quietly:
	 * a retype that still fails is reported with the reason.
	 */
	private void applyToVariable(String name, JsonObject t, DataType dt) {
		String fnAddr = str(t, "function");
		String varName = str(t, "name");
		Function f = getFunctionAt(toAddr(fnAddr));
		if (f == null) {
			targetProblems.add("APPLY VARIABLE " + name + ": no function at " + fnAddr);
			return;
		}
		Variable found = null;
		for (Variable var : f.getAllVariables()) {
			if (varName.equals(var.getName())) {
				found = var;
				break;
			}
		}
		if (found == null) {
			// The layout may name a stack slot by an explicit offset, or by the
			// display name Ghidra puts on it -- auStack_50, local_30 -- which
			// encodes the same offset and is regenerated on every decompile
			// rather than stored. Both reach the same slot.
			// Written out rather than as a ternary on purpose: mixing an int
			// branch with an Integer branch makes Java unbox both, and the null
			// this method legitimately returns then throws.
			Integer offset;
			if (t.has("stack_offset") && !t.get("stack_offset").isJsonNull()) {
				offset = Integer.valueOf(t.get("stack_offset").getAsInt());
			}
			else {
				offset = stackOffsetFromDisplayName(varName);
			}
			if (offset != null) {
				for (Variable var : f.getAllVariables()) {
					if (var.isStackVariable() && var.getStackOffset() == offset) {
						found = var;
						break;
					}
				}
			}
		}
		if (found == null) {
			targetProblems.add("APPLY VARIABLE " + name + ": " + f.getName()
				+ " has no variable called " + varName
				+ (varName.matches("[a-z]{1,3}Var\\d+")
					? " (that is a decompiler temporary, not a stored variable;"
						+ " nothing in the database can carry a type there)"
					: ""));
			return;
		}
		try {
			found.setDataType(dt, SourceType.USER_DEFINED);
			variablesTyped++;
			return;
		}
		catch (Exception first) {
			if (!found.isStackVariable()) {
				targetProblems.add("APPLY VARIABLE " + name + " " + f.getName() + "." + varName
					+ ": " + first.getMessage());
				return;
			}
		}

		int base = found.getStackOffset();
		int end = base + dt.getLength();
		List<Variable> displaced = new ArrayList<>();
		for (Variable var : f.getAllVariables()) {
			if (var == found || !var.isStackVariable()) {
				continue;
			}
			int o = var.getStackOffset();
			if (o > base && o < end) {
				displaced.add(var);
			}
		}
		if (displaced.isEmpty()) {
			targetProblems.add("APPLY VARIABLE " + name + " " + f.getName() + "." + varName
				+ ": storage conflict with nothing this script can see to clear");
			return;
		}
		StringBuilder cleared = new StringBuilder();
		for (Variable var : displaced) {
			cleared.append(cleared.length() == 0 ? "" : ", ").append(var.getName());
			try {
				f.removeVariable(var);
			}
			catch (Exception e) {
				targetProblems.add("APPLY VARIABLE " + name + " " + f.getName() + "." + varName
					+ ": could not clear " + var.getName() + ": " + e.getMessage());
				return;
			}
		}
		try {
			found.setDataType(dt, SourceType.USER_DEFINED);
			variablesTyped++;
			println("  cleared " + displaced.size() + " overlapping local(s) in " + f.getName()
				+ " to fit " + name + " at " + varName + ": " + cleared);
		}
		catch (Exception second) {
			targetProblems.add("APPLY VARIABLE " + name + " " + f.getName() + "." + varName
				+ ": still refused after clearing " + cleared + ": " + second.getMessage());
		}
	}

	/**
	 * The stack offset hiding inside Ghidra's display name for a stack slot.
	 * "local_30" and "auStack_50" both mean "0x30 / 0x50 bytes below the frame
	 * pointer"; the prefix letters are the decompiler's guess at the type and
	 * change from one decompile to the next, which is exactly why the name is
	 * not something to look up.
	 *
	 * @return the offset, or null when the name is not one of these forms
	 */
	private static Integer stackOffsetFromDisplayName(String varName) {
		String digits = null;
		if (varName.startsWith("local_")) {
			digits = varName.substring("local_".length());
		}
		else {
			int at = varName.indexOf("Stack_");
			if (at > 0) {
				digits = varName.substring(at + "Stack_".length());
			}
		}
		if (digits == null || digits.isEmpty()) {
			return null;
		}
		try {
			return -Integer.parseInt(digits, 16);
		}
		catch (NumberFormatException e) {
			return null;   // local_res4 and friends are parameters, not frame slots
		}
	}

	/**
	 * True when a type spelling names a struct that simply has no layout file
	 * yet, as opposed to a spelling nothing could ever resolve. Only the base
	 * name matters: "fdps_unit_record[31]" and "fdps_unit_record *" both wait on
	 * the same file.
	 */
	private boolean awaitingLayout(String spec) {
		String s = spec.trim();
		int bracket = s.indexOf('[');
		if (bracket >= 0) {
			s = s.substring(0, bracket);
		}
		while (s.endsWith("*")) {
			s = s.substring(0, s.length() - 1).trim();
		}
		return !s.isEmpty() && candidateNames().contains(s.trim());
	}

	private java.util.Set<String> candidates;

	/**
	 * Every struct name the ticket knows about, from structs.json next to the
	 * layout directory. A field naming one of these is waiting on a layout that
	 * is coming; a field naming anything else is a spelling mistake.
	 */
	private java.util.Set<String> candidateNames() {
		if (candidates != null) {
			return candidates;
		}
		candidates = new java.util.HashSet<>();
		if (structDir == null || structDir.getParentFile() == null) {
			return candidates;
		}
		File f = new File(structDir.getParentFile(), "structs.json");
		if (!f.isFile()) {
			return candidates;
		}
		try {
			JsonObject o = JsonParser.parseString(new String(
				java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8))
				.getAsJsonObject();
			for (JsonElement e : o.getAsJsonArray("proposals")) {
				candidates.add(e.getAsJsonObject().get("name").getAsString());
			}
		}
		catch (Exception e) {
			println("WARN: could not read structs.json: " + e.getMessage());
		}
		return candidates;
	}

	/** Same spelling rules as ApplyGlobalVerdicts: a name, stars, one dimension. */
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
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		if ("func_ptr".equals(name)) {
			// Same placeholder ApplyGlobalVerdicts uses for dispatch tables:
			// void (void), because what a slot really takes is an emit-stage
			// question. What matters here is that it is a code pointer.
			DataType fn = dtm.getDataType(CATEGORY, "void_fn");
			if (fn == null) {
				FunctionDefinitionDataType def =
					new FunctionDefinitionDataType(CATEGORY, "void_fn", dtm);
				def.setReturnType(ghidra.program.model.data.VoidDataType.dataType);
				fn = dtm.addDataType(def, DataTypeConflictHandler.KEEP_HANDLER);
			}
			return new PointerDataType(fn);
		}
		DataType dt = dtm.getDataType(CATEGORY, name);
		if (dt != null) {
			return dt;
		}
		dt = dtm.getDataType("/" + name);
		if (dt != null) {
			return dt;
		}
		DataType[] found = getDataTypes(name);
		return found != null && found.length > 0 ? found[0] : null;
	}

	private static String str(JsonObject o, String key) {
		if (o == null) {
			return "";
		}
		JsonElement e = o.get(key);
		return e == null || e.isJsonNull() ? "" : e.getAsString();
	}

	private static int num(JsonObject o, String key) {
		JsonElement e = o.get(key);
		return e == null || e.isJsonNull() ? -1 : e.getAsInt();
	}

	private static JsonArray arr(JsonObject o, String key) {
		JsonElement e = o.get(key);
		return e != null && e.isJsonArray() ? e.getAsJsonArray() : null;
	}
}
