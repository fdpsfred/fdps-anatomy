// Transcribe one round of ticket 14.2 re-review verdicts into Ghidra.
//
// Each verdict was produced by an agent that re-read one function's assembly and
// answered four independent questions about it: which pool it belongs to, what
// it should be called, where its boundary and signature are, and whether the
// plate comment still matches the code.  This script only writes them down.  It
// judges nothing: a verdict it cannot apply is reported, never worked around.
//
// Boundary changes are the reason this script is not ApplyPoolVerdicts.  Moving
// a function's body invalidates the verdicts of everything the move touched, so
// every boundary change is appended to boundary_changes.jsonl next to the
// verdict directory; build_packets.py retires the verdicts in those ranges and
// puts the functions back on the worklist.
//
// Usage: run_ghidra_script with two or more arguments:
//   <verdict dir> all           apply every verdict file in the directory
//   <verdict dir> <addr> ...    apply only these functions (8 hex digits)
//
// Verdict file <addr>.json -- see rereview_ticket14_2.js for the full shape.
// The fields read here:
//   addr
//   pool.verdict            fdps | crt | ail | binary_artifact | unknown
//   name.verdict            the symbol name, or empty to leave the name alone
//   boundary.fix            none | set_body | split | delete
//   boundary.start/end      for set_body, the range the body should cover
//   boundary.split_at       for split, where the second function begins
//   signature.cc            a Ghidra calling convention name, or empty
//   signature.prototype     a C prototype WITHOUT the convention, or empty
//   plate.text              the full replacement plate comment, or empty
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.symbol.SourceType;

public class ApplyRereviewVerdicts extends GhidraScript {

	private final List<String> problems = new ArrayList<>();
	private final List<String> boundaryChanges = new ArrayList<>();
	private final Map<String, Integer> pools = new TreeMap<>();

	private int applied;
	private int renamed;
	private int retyped;
	private int replated;
	private int bodyChanged;
	private int created;
	private int deleted;

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyRereviewVerdicts <verdict dir> all|<addr>...");
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

		for (String key : wanted) {
			applyOne(dir, key);
		}

		if (!boundaryChanges.isEmpty()) {
			appendBoundaryLog(new File(dir.getParentFile(), "boundary_changes.jsonl"));
		}

		println("verdicts applied = " + applied + " of " + wanted.size());
		println("renamed = " + renamed);
		println("signatures set = " + retyped);
		println("plate comments written = " + replated);
		println("bodies changed = " + bodyChanged);
		println("functions created = " + created);
		println("functions deleted = " + deleted);
		println("pool breakdown = " + pools);
		println("boundary changes = " + boundaryChanges.size());
		for (String b : boundaryChanges) {
			println("  AFFECTED " + b);
		}
		println("problems = " + problems.size());
		for (String p : problems) {
			println("  " + p);
		}
	}

	private void applyOne(File dir, String key) {
		File f = new File(dir, key + ".json");
		if (!f.isFile()) {
			problems.add("NO VERDICT FILE " + key);
			return;
		}
		JsonObject v;
		try {
			v = JsonParser.parseString(
				new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8)).getAsJsonObject();
		}
		catch (Exception e) {
			problems.add("UNPARSEABLE " + key + ": " + e.getMessage());
			return;
		}
		Address at = addr(key);
		Function fn = at == null ? null : getFunctionAt(at);
		if (fn == null) {
			problems.add("NO FUNCTION AT " + key);
			return;
		}

		// Boundary first: everything else describes the function the boundary
		// defines, and a delete makes the rest moot.
		if (!applyBoundary(key, v, fn)) {
			return;
		}
		fn = getFunctionAt(at);
		if (fn == null) {
			// A delete verdict: nothing left to name or tag.
			applied++;
			return;
		}

		applyPool(key, v, fn);
		applyName(key, v, fn);
		applySignature(key, v, fn);
		applyPlate(key, v, fn, at);
		applied++;
	}

	/** @return false when the verdict cannot be applied at all. */
	private boolean applyBoundary(String key, JsonObject v, Function fn) {
		JsonObject b = obj(v, "boundary");
		String fix = str(b, "fix");
		if (fix == null || fix.isEmpty() || "none".equals(fix)) {
			return true;
		}
		try {
			if ("delete".equals(fix)) {
				Address min = fn.getBody().getMinAddress();
				Address max = fn.getBody().getMaxAddress();
				removeFunction(fn);
				deleted++;
				noteBoundary(key, min, max, "delete");
				return true;
			}
			if ("set_body".equals(fix)) {
				Address start = addr(str(b, "start"));
				Address end = addr(str(b, "end"));
				if (start == null || end == null || end.compareTo(start) < 0) {
					problems.add("BAD BOUNDARY RANGE " + key + " " + str(b, "start") + "-" + str(b, "end"));
					return true;
				}
				Address oldMin = fn.getBody().getMinAddress();
				Address oldMax = fn.getBody().getMaxAddress();
				fn.setBody(new AddressSet(start, end));
				bodyChanged++;
				noteBoundary(key, min(oldMin, start), max(oldMax, end), "set_body");
				return true;
			}
			if ("split".equals(fix)) {
				Address splitAt = addr(str(b, "split_at"));
				if (splitAt == null) {
					problems.add("SPLIT WITHOUT split_at " + key);
					return true;
				}
				Address oldMax = fn.getBody().getMaxAddress();
				if (splitAt.compareTo(fn.getEntryPoint()) <= 0 || splitAt.compareTo(oldMax) > 0) {
					problems.add("SPLIT POINT OUTSIDE BODY " + key + " at " + splitAt);
					return true;
				}
				fn.setBody(new AddressSet(fn.getEntryPoint(), splitAt.subtract(1)));
				Function made = createFunction(splitAt, null);
				if (made == null) {
					problems.add("SPLIT FAILED to create a function at " + splitAt + " for " + key);
				}
				else {
					created++;
				}
				bodyChanged++;
				noteBoundary(key, fn.getEntryPoint(), oldMax, "split");
				return true;
			}
			problems.add("UNKNOWN BOUNDARY FIX " + key + " " + fix);
		}
		catch (Exception e) {
			problems.add("BOUNDARY FAILED " + key + " " + fix + ": "
				+ e.getClass().getSimpleName() + " " + e.getMessage());
		}
		return true;
	}

	private void applyPool(String key, JsonObject v, Function fn) {
		String pool = str(obj(v, "pool"), "verdict");
		if (pool == null || pool.isEmpty()) {
			pool = "unknown";
		}
		pools.merge(pool, 1, Integer::sum);
		String want = "unknown".equals(pool) ? null : "pool_" + pool;
		boolean has = false;
		for (FunctionTag t : new ArrayList<>(fn.getTags())) {
			String name = t.getName();
			if (!name.startsWith("pool_")) {
				continue;
			}
			if (want != null && name.equals(want)) {
				has = true;
				continue;
			}
			fn.removeTag(name);
		}
		if (want != null && !has) {
			fn.addTag(want);
		}
	}

	private void applyName(String key, JsonObject v, Function fn) {
		String name = str(obj(v, "name"), "verdict");
		if (name == null || name.isEmpty() || name.equals(fn.getName())) {
			return;
		}
		// A verdict that settles on FUN_<addr> is giving the name back: the symbol
		// belongs to another address, and this one has none of its own. Ghidra
		// refuses that string as a user-defined name because it is the shape it
		// generates itself, so the way to say it is to clear the symbol and let the
		// default come back.
		if (name.equalsIgnoreCase("FUN_" + key)) {
			try {
				fn.setName(null, SourceType.DEFAULT);
				renamed++;
			}
			catch (Exception e) {
				problems.add("RESET TO DEFAULT FAILED " + key + ": " + e.getMessage());
			}
			return;
		}
		// A name already worn by a different address is a contradiction between two
		// verdicts, not something to paper over with a suffix: one of the two
		// identifications is wrong and the gate has to see it.
		for (Function other : getGlobalFunctions(name)) {
			if (!other.getEntryPoint().equals(fn.getEntryPoint())) {
				problems.add("NAME TAKEN " + key + " -> " + name + " (held by "
					+ hex8(other.getEntryPoint().getOffset()) + "), left unchanged");
				return;
			}
		}
		try {
			fn.setName(name, SourceType.USER_DEFINED);
			renamed++;
		}
		catch (Exception e) {
			problems.add("RENAME FAILED " + key + " -> " + name + ": " + e.getMessage());
		}
	}

	private void applySignature(String key, JsonObject v, Function fn) {
		JsonObject s = obj(v, "signature");
		// A convention the assembly did not confirm stays out of the database. The
		// guess is recorded in the plate comment instead, where a reader can see
		// that it is one.
		if (s != null && s.has("assumed") && !s.get("assumed").isJsonNull()
			&& s.get("assumed").getAsBoolean()) {
			return;
		}
		// The name inside the prototype is load-bearing: ApplyFunctionSignatureCmd
		// renames the function to whatever the string says. A verdict that keeps
		// the FUN_ name but writes its prototype as "int f(int unit)" would
		// silently rename the function to f, and the naming gate only notices when
		// the result happens to break a prefix rule. The name axis decides the
		// name; the signature axis does not get a say.
		String proto = forceName(cleanPrototype(str(s, "prototype")), fn.getName());
		String cc = str(s, "cc");
		if (proto != null && !proto.isEmpty()) {
			try {
				FunctionSignatureParser parser =
					new FunctionSignatureParser(currentProgram.getDataTypeManager(), null);
				FunctionDefinitionDataType sig = parser.parse(fn.getSignature(), proto);
				ApplyFunctionSignatureCmd cmd =
					new ApplyFunctionSignatureCmd(fn.getEntryPoint(), sig, SourceType.USER_DEFINED);
				if (!cmd.applyTo(currentProgram, monitor)) {
					problems.add("SIGNATURE REJECTED " + key + ": " + cmd.getStatusMsg());
				}
				else {
					retyped++;
				}
			}
			catch (Exception e) {
				problems.add("SIGNATURE PARSE FAILED " + key + " [" + proto + "]: " + e.getMessage());
			}
		}
		if (cc != null && !cc.isEmpty() && !cc.equals(fn.getCallingConventionName())) {
			try {
				fn.setCallingConvention(cc);
			}
			catch (Exception e) {
				problems.add("CONVENTION FAILED " + key + " -> " + cc + ": " + e.getMessage());
			}
		}
	}

	/**
	 * Strip qualifiers the program's DataTypeManager does not model.
	 * FunctionSignatureParser resolves every type name against the program, and
	 * "const char *" is not a type there -- the whole prototype is then rejected
	 * and the function keeps Ghidra's guess. The qualifier carries no information
	 * Ghidra would store anyway, so dropping it loses nothing.
	 */
	private static String cleanPrototype(String proto) {
		if (proto == null || proto.isEmpty()) {
			return proto;
		}
		// A calling convention inside the prototype string makes Ghidra read
		// "int __cdecl" as the return type and reject the whole signature. The
		// convention has its own field and never reaches the parser from here.
		String out = proto.replaceAll("\\b(__cdecl|__watcall|__stdcall|__fastcall|__pascal)\\b\\s*", "");
		out = out.replaceAll("\\b(const|volatile|restrict)\\b\\s*", "");
		// Typedefs from the C standard headers are not in this program's type
		// manager either -- nothing declared them, because nothing here has source.
		// The mapping is the one the 32-bit DOS target uses, so it changes no
		// meaning; leaving them in would throw the whole prototype away.
		out = out.replaceAll("\\bsize_t\\b", "uint");
		out = out.replaceAll("\\bssize_t\\b", "int");
		out = out.replaceAll("\\bptrdiff_t\\b", "int");
		out = out.replaceAll("\\bintptr_t\\b", "int");
		out = out.replaceAll("\\buintptr_t\\b", "uint");
		out = out.replaceAll("\\bbool\\b", "char");
		return out.trim();
	}

	/** Replace whatever function name the prototype carries with the real one. */
	private static String forceName(String proto, String name) {
		if (proto == null || proto.isEmpty() || name == null || name.isEmpty()) {
			return proto;
		}
		int paren = proto.indexOf('(');
		if (paren <= 0) {
			return proto;
		}
		String head = proto.substring(0, paren);
		int cut = Math.max(head.lastIndexOf(' '), head.lastIndexOf('*'));
		if (cut < 0) {
			return proto;
		}
		// Ghidra splits the name off at the last space, so a pointer return type
		// must be separated from the name: "char * strncpy(...)", never
		// "char *strncpy(...)", which it reads as a name of "*strncpy".
		return head.substring(0, cut + 1).trim() + " " + name + proto.substring(paren);
	}

	private void applyPlate(String key, JsonObject v, Function fn, Address at) {
		String text = str(obj(v, "plate"), "text");
		if (text == null || text.isEmpty()) {
			return;
		}
		try {
			setPlateComment(at, text.trim());
			replated++;
		}
		catch (Exception e) {
			problems.add("PLATE FAILED " + key + ": " + e.getMessage());
		}
	}

	private void noteBoundary(String key, Address min, Address max, String kind) {
		boundaryChanges.add("{\"addr\":\"" + key + "\",\"kind\":\"" + kind + "\",\"min\":\""
			+ hex8(min.getOffset()) + "\",\"max\":\"" + hex8(max.getOffset()) + "\"}");
	}

	private void appendBoundaryLog(File file) {
		try (PrintWriter pw = new PrintWriter(new OutputStreamWriter(
			new FileOutputStream(file, true), StandardCharsets.UTF_8))) {
			for (String line : boundaryChanges) {
				pw.println(line);
			}
		}
		catch (Exception e) {
			problems.add("COULD NOT WRITE boundary_changes.jsonl: " + e.getMessage());
		}
	}

	private static Address min(Address a, Address b) {
		return a.compareTo(b) <= 0 ? a : b;
	}

	private static Address max(Address a, Address b) {
		return a.compareTo(b) >= 0 ? a : b;
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

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private static JsonObject obj(JsonObject o, String key) {
		JsonElement e = o == null ? null : o.get(key);
		return e != null && e.isJsonObject() ? e.getAsJsonObject() : null;
	}

	private static String str(JsonObject o, String key) {
		JsonElement e = o == null ? null : o.get(key);
		return e == null || e.isJsonNull() ? null : e.getAsString();
	}
}
