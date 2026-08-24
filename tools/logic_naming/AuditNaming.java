// Ticket 15's own gate: is every game-logic function readable by a human yet?
//
// The baseline audit (tools/ghidra_baseline/AuditGhidraBaseline.java) answers
// "is the program still structurally sound"; this one answers "did the naming
// pass actually land". They run together after every round.
//
// The distinction that makes this runnable mid-ticket is pending vs violation:
//
//   pending    still carries its Ghidra-generated FUN_ name -- nobody has judged
//              it yet. Expected while the ticket is in flight; the ticket is
//              done when this reaches zero.
//   violation  something a landed verdict got wrong: a named function that still
//              has a param_N, a name that breaks the prefix rule, two addresses
//              claiming one symbol, a named function with no plate comment, a
//              convention this binary does not use.
//
// A violation is never acceptable, at any point in the run. A round that lands
// one has to fix it before the next round starts.
//
// Read-only. Usage: run_ghidra_script with an optional output directory.
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class AuditNaming extends GhidraScript {

	private static final String EXPECTED_PROGRAM = "FDPS.LE";
	private static final String DEFAULT_OUT_DIR =
		"C:/Users/fdpsf/Documents/fdps-anatomy/workspace/logic_naming";
	/** How many violations to print before the rest go to the file only. */
	private static final int PRINT_LIMIT = 40;

	private final List<String> violations = new ArrayList<>();
	private final List<String> pending = new ArrayList<>();

	@Override
	public void run() throws Exception {
		if (currentProgram == null || !EXPECTED_PROGRAM.equals(currentProgram.getName())) {
			throw new IllegalStateException("current program is " +
				(currentProgram == null ? "(none)" : currentProgram.getName()) +
				", expected " + EXPECTED_PROGRAM);
		}
		String[] argv = getScriptArgs();
		File outDir = new File(argv.length > 0 ? argv[0] : DEFAULT_OUT_DIR);

		// Symbol collisions are program-wide: a game function taking a name a CRT
		// or AIL function already holds is exactly as wrong as two game functions
		// colliding, and wlink would resolve the wrong one.
		Map<String, List<Function>> byName = new HashMap<>();
		List<Function> fdps = new ArrayList<>();
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			Function f = it.next();
			String name = f.getName();
			// Names Ghidra generated for itself are not anyone's claim, so two of
			// them landing on the same string means nothing. A thunk is the case
			// that makes the string test insufficient: Ghidra reports the target's
			// name as the thunk's own, so the pair reads as a collision even though
			// only one address ever claimed the symbol. What separates a claim from
			// an echo is the symbol source, not the spelling.
			Symbol sym = f.getSymbol();
			if (sym != null && sym.getSource() != SourceType.DEFAULT) {
				byName.computeIfAbsent(name, k -> new ArrayList<>()).add(f);
			}
			if ("fdps".equals(poolOf(f))) {
				fdps.add(f);
			}
		}
		for (Map.Entry<String, List<Function>> e : byName.entrySet()) {
			if (e.getValue().size() > 1) {
				StringBuilder sb = new StringBuilder("NAME COLLISION " + e.getKey() + " held by");
				for (Function f : e.getValue()) {
					sb.append(' ').append(hex8(f.getEntryPoint().getOffset()));
				}
				violations.add(sb.toString());
			}
		}

		Map<String, Integer> ccHist = new TreeMap<>();
		int named = 0;
		int withPlate = 0;
		int paramCount = 0;
		for (Function f : fdps) {
			String key = hex8(f.getEntryPoint().getOffset());
			String name = f.getName();
			if (name.startsWith("FUN_") || name.startsWith("thunk_FUN_")) {
				pending.add(key);
				continue;
			}
			named++;

			// The one exemption in naming.md: the C entry point has to be called
			// main, because the Watcom cmain386 contract names that symbol.
			if (!name.startsWith("fdps_") && !name.equals("main")) {
				violations.add("PREFIX " + key + " " + name
					+ " (pool_fdps names take fdps_, only main is exempt)");
			}
			// A name that still reads as an address is a name that was never
			// decided, whatever prefix it wears.
			if (name.matches(".*_?[0-9a-f]{8}$")) {
				violations.add("ADDRESS IN NAME " + key + " " + name);
			}

			for (Parameter p : f.getParameters()) {
				paramCount++;
				if (p.getName().matches("param_\\d+")) {
					violations.add("DEFAULT PARAMETER " + key + " " + name + " " + p.getName());
				}
			}

			String cc = String.valueOf(f.getCallingConventionName());
			ccHist.merge(cc, 1, Integer::sum);
			// This binary is built with wcc386 -4s, so stack-based __cdecl is the
			// default and hand-written assembly is __watcall. Anything else means a
			// verdict wrote a convention it had no evidence for.
			if (!"__cdecl".equals(cc) && !"__watcall".equals(cc)) {
				violations.add("CONVENTION " + key + " " + name + " " + cc);
			}

			String plate = getPlateComment(f.getEntryPoint());
			if (plate == null || plate.trim().isEmpty()) {
				violations.add("NO PLATE " + key + " " + name);
			}
			else {
				withPlate++;
			}
		}

		StringBuilder report = new StringBuilder();
		report.append("# Ticket 15 naming audit\n");
		report.append("pool_fdps functions = ").append(fdps.size()).append('\n');
		report.append("named = ").append(named).append('\n');
		report.append("pending (still FUN_) = ").append(pending.size()).append(" (target 0)\n");
		report.append("named with plate comment = ").append(withPlate).append('\n');
		report.append("parameters on named functions = ").append(paramCount).append('\n');
		report.append("calling conventions = ").append(ccHist).append('\n');
		report.append("violations = ").append(violations.size()).append(" (target 0)\n");
		for (String v : violations) {
			report.append("  ").append(v).append('\n');
		}
		report.append("\n# Pending addresses\n");
		for (String p : pending) {
			report.append(p).append('\n');
		}

		outDir.mkdirs();
		File out = new File(outDir, "naming_audit.txt");
		try (PrintWriter pw = new PrintWriter(
			new OutputStreamWriter(new FileOutputStream(out), StandardCharsets.UTF_8))) {
			pw.print(report.toString());
		}

		println("pool_fdps functions = " + fdps.size());
		println("named = " + named);
		println("pending (still FUN_) = " + pending.size() + " (target 0)");
		println("named with plate comment = " + withPlate);
		println("parameters on named functions = " + paramCount);
		println("calling conventions = " + ccHist);
		println("violations = " + violations.size() + " (target 0)");
		for (int i = 0; i < violations.size() && i < PRINT_LIMIT; i++) {
			println("  " + violations.get(i));
		}
		if (violations.size() > PRINT_LIMIT) {
			println("  ... " + (violations.size() - PRINT_LIMIT) + " more, see naming_audit.txt");
		}
		println("audit written to " + out.getAbsolutePath());
	}

	private static String poolOf(Function f) {
		for (FunctionTag t : f.getTags()) {
			if (t.getName().startsWith("pool_")) {
				return t.getName().substring(5);
			}
		}
		return "";
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}
}
