// Transcribe one round of ticket 15 naming verdicts into Ghidra.
//
// Each verdict came from an agent that read one game-logic function's assembly
// and decided four things about it: what it should be called, what its calling
// convention really is, what each of its parameters is for, and what its plate
// comment should say. This script writes those down. It judges nothing: a
// verdict it cannot apply is reported and left alone (ADR-0007 5.3).
//
// Ticket 15 does not move boundaries -- ticket 14.2 settled those. A verdict
// that found a boundary problem records it in boundary_issue, and this script
// only counts and reports them, so the workflow can put them in front of a human
// instead of quietly reshaping the program during a naming pass.
//
// Usage: run_ghidra_script with two or more arguments:
//   <verdict dir> all           apply every verdict file in the directory
//   <verdict dir> <addr> ...    apply only these functions (8 hex digits)
//
// Verdict file <addr>.json -- see naming_ticket15.js for the full shape.
// The fields read here:
//   addr
//   name.verdict            the symbol name; empty leaves the name alone
//   signature.cc            a Ghidra calling convention name, or empty
//   signature.prototype     a C prototype WITHOUT the convention, carrying the
//                           semantic parameter names
//   signature.assumed       true keeps a guessed convention out of the database
//   plate.text              the full replacement plate comment, or empty
//   boundary_issue          non-empty means "something is wrong with the body";
//                           reported, never acted on
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.symbol.SourceType;

public class ApplyNamingVerdicts extends GhidraScript {

	private final List<String> problems = new ArrayList<>();
	private final List<String> boundaryIssues = new ArrayList<>();

	private int applied;
	private int renamed;
	private int retyped;
	private int replated;
	private int conventionSet;

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyNamingVerdicts <verdict dir> all|<addr>...");
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

		println("verdicts applied = " + applied + " of " + wanted.size());
		println("renamed = " + renamed);
		println("signatures set = " + retyped);
		println("conventions set = " + conventionSet);
		println("plate comments written = " + replated);
		println("boundary issues reported = " + boundaryIssues.size());
		for (String b : boundaryIssues) {
			println("  BOUNDARY " + b);
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

		String issue = str(v, "boundary_issue");
		if (issue != null && !issue.isEmpty()) {
			boundaryIssues.add(key + ": " + issue.replace("\n", " "));
		}

		// Name first: the signature parser renames the function to whatever the
		// prototype string says, so the prototype has to be rewritten to carry the
		// name this verdict actually settled on, and that means knowing it.
		applyName(key, v, fn);
		fn = getFunctionAt(at);
		applySignature(key, v, fn);
		applyPlate(key, v, fn, at);
		applied++;
	}

	private void applyName(String key, JsonObject v, Function fn) {
		String name = str(obj(v, "name"), "verdict");
		if (name == null || name.isEmpty() || name.equals(fn.getName())) {
			return;
		}
		// Ticket 15's whole point is that no default name survives it, so a verdict
		// that hands back FUN_<addr> has not done the job. Say so rather than
		// writing it.
		if (name.startsWith("FUN_") || name.startsWith("thunk_FUN_")) {
			problems.add("DEFAULT NAME AS VERDICT " + key + " -> " + name);
			return;
		}
		// A name already worn by a different address is a contradiction between two
		// verdicts, not something to paper over with a suffix: one of the two is
		// wrong about what the function does, and the gate has to see it.
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
		if (s == null) {
			return;
		}
		// A convention the assembly did not confirm stays out of the database; the
		// guess belongs in the plate comment, where a reader can see it is one.
		boolean assumed = s.has("assumed") && !s.get("assumed").isJsonNull()
			&& s.get("assumed").getAsBoolean();

		String proto = forceName(cleanPrototype(str(s, "prototype")), fn.getName());
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
					// The signature is where parameter names land, so this is the
					// place to notice they did not: a prototype written with
					// param_1 leaves a default name behind and the gate would only
					// catch it at the end of the round.
					for (Parameter p : getFunctionAt(fn.getEntryPoint()).getParameters()) {
						if (p.getName().matches("param_\\d+")) {
							problems.add("DEFAULT PARAMETER NAME " + key + " "
								+ p.getName() + " in [" + proto + "]");
						}
					}
				}
			}
			catch (Exception e) {
				problems.add("SIGNATURE PARSE FAILED " + key + " [" + proto + "]: " + e.getMessage());
			}
		}

		String cc = str(s, "cc");
		if (!assumed && cc != null && !cc.isEmpty() && !cc.equals(fn.getCallingConventionName())) {
			try {
				fn.setCallingConvention(cc);
				conventionSet++;
			}
			catch (Exception e) {
				problems.add("CONVENTION FAILED " + key + " -> " + cc + ": " + e.getMessage());
			}
		}
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

	/**
	 * Strip qualifiers the program's DataTypeManager does not model.
	 * FunctionSignatureParser resolves every type name against the program, and
	 * "const char *" is not a type there -- the whole prototype is then rejected
	 * and the function keeps Ghidra's guess.
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
		// must be separated from the name: "char * f(...)", never "char *f(...)",
		// which it reads as a name of "*f".
		return head.substring(0, cut + 1).trim() + " " + name + proto.substring(paren);
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
