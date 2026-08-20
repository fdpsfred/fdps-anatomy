// The gate ticket 14.2 adds on top of the baseline audit: after a round of
// renames, no two addresses may claim the same vendor symbol, and no name may
// contradict the pool it sits in.
//
// Two addresses wearing one library symbol is not a cosmetic clash. The name is
// what wlink will resolve against the real .LIB, and it is also the evidence for
// the pool judgement -- so a duplicate means one of the two identifications is
// wrong and has to be re-read, not suffixed away.
//
// Read-only: the script never writes to the program database.
//
// Usage (Ghidra MCP): run_ghidra_script with this absolute path.
//
//@category FDPS
//@runtime Java

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;

public class AuditNames extends GhidraScript {

	private static final String EXPECTED_PROGRAM = "FDPS.LE";

	@Override
	public void run() throws Exception {
		if (currentProgram == null || !EXPECTED_PROGRAM.equals(currentProgram.getName())) {
			throw new IllegalStateException("current program is " +
				(currentProgram == null ? "(none)" : currentProgram.getName()) +
				", expected " + EXPECTED_PROGRAM);
		}

		Map<String, List<String>> byName = new TreeMap<>();
		List<String> prefixMismatch = new ArrayList<>();
		List<String> tagProblems = new ArrayList<>();
		List<String> noPoolTag = new ArrayList<>();
		List<String> suffixed = new ArrayList<>();
		List<String> thunks = new ArrayList<>();
		Map<String, Integer> poolCounts = new TreeMap<>();
		int total = 0;

		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			Function f = it.next();
			total++;
			String name = f.getName();
			String at = hex8(f.getEntryPoint().getOffset());
			// A thunk does not have a name of its own: Ghidra shows it the name of
			// the function it jumps to. Counting that as a second claim on the
			// symbol would report a clash that does not exist, and "fixing" it
			// would mean renaming a thunk away from the body it forwards to.
			if (f.isThunk()) {
				thunks.add(at + " " + name);
			}
			else {
				byName.computeIfAbsent(name, k -> new ArrayList<>()).add(at);
			}

			List<String> pools = new ArrayList<>();
			for (FunctionTag t : f.getTags()) {
				if (t.getName().startsWith("pool_")) {
					pools.add(t.getName().substring(5));
				}
			}
			if (pools.isEmpty()) {
				// An honest "unknown" verdict removes the tag, so this is a legal
				// intermediate state that the rescan is expected to clear. It is
				// reported and does not fail the gate; two tags on one function is
				// a different thing entirely and does.
				noPoolTag.add(at + " " + name);
				continue;
			}
			if (pools.size() > 1) {
				tagProblems.add(at + " " + name + ": " + pools.size() + " pool tags " + pools);
				continue;
			}
			String pool = pools.get(0);
			poolCounts.merge(pool, 1, Integer::sum);

			String mismatch = prefixCheck(name, pool);
			if (mismatch != null) {
				prefixMismatch.add(at + " " + name + ": " + mismatch);
			}
			// A chosen name that had to carry its own address to get past a clash.
			// Ghidra's own FUN_<addr> placeholders end the same way and mean nothing.
			if (name.endsWith("_" + at) && !isAutoName(name)) {
				suffixed.add(at + " " + name);
			}
		}

		// Two addresses claiming one chosen symbol is a contradiction between two
		// verdicts and fails the gate. Two addresses sharing a name Ghidra
		// generated says nothing about anybody's judgement, so it is reported and
		// not counted.
		List<String> collisions = new ArrayList<>();
		List<String> autoCollisions = new ArrayList<>();
		for (Map.Entry<String, List<String>> e : byName.entrySet()) {
			if (e.getValue().size() <= 1) {
				continue;
			}
			if (isAutoName(e.getKey())) {
				autoCollisions.add(e.getKey() + " @ " + e.getValue());
			}
			else {
				collisions.add(e.getKey() + " @ " + e.getValue());
			}
		}

		println("# AuditNames");
		println("functions = " + total);
		println("pool breakdown = " + poolCounts);
		println("name collisions = " + collisions.size());
		for (String c : collisions) {
			println("  COLLISION " + c);
		}
		println("prefix vs pool mismatches = " + prefixMismatch.size());
		for (String p : prefixMismatch) {
			println("  MISMATCH " + p);
		}
		println("pool tag problems = " + tagProblems.size());
		for (String p : tagProblems) {
			println("  TAG " + p);
		}
		println("functions with no pool tag = " + noPoolTag.size());
		for (String p : noPoolTag) {
			println("  UNKNOWN POOL " + p);
		}
		println("thunks wearing the name of their target = " + thunks.size());
		println("collisions on a generated name = " + autoCollisions.size());
		for (String c : autoCollisions) {
			println("  AUTO-COLLISION " + c);
		}
		println("chosen names carrying their own address as a suffix = " + suffixed.size());
		for (String s : suffixed) {
			println("  SUFFIXED " + s);
		}
		println("# Gate");
		println("collisions " + collisions.size());
		println("prefix_mismatches " + prefixMismatch.size());
		println("tag_problems " + tagProblems.size());
		println("no_pool_tag " + noPoolTag.size() + " (reported, does not fail the gate)");
		println("clean " + (collisions.isEmpty() && prefixMismatch.isEmpty() && tagProblems.isEmpty()));
	}

	/**
	 * The prefix rules from rebuild_info/naming.md that a machine can check.
	 * Vendor symbols carry no project prefix at all, so the check runs the other
	 * way for crt: a crt function must not wear a name that claims another pool.
	 */
	private String prefixCheck(String name, String pool) {
		boolean fdpsPrefix = name.startsWith("fdps_") || name.startsWith("data_fdps_");
		boolean ailPrefix = name.startsWith("AIL_");
		boolean artifactPrefix = name.startsWith("binary_artifact_");
		boolean unnamed = name.startsWith("FUN_") || name.startsWith("SUB_");

		switch (pool) {
			case "fdps":
				if (ailPrefix || artifactPrefix) {
					return "pool is fdps but the name claims another pool";
				}
				if (!fdpsPrefix && !unnamed && !"main".equals(name)) {
					return "pool is fdps but the name has no fdps_ prefix";
				}
				return null;
			case "ail":
				if (fdpsPrefix || artifactPrefix) {
					return "pool is ail but the name claims another pool";
				}
				return null;
			case "crt":
				if (fdpsPrefix || ailPrefix || artifactPrefix) {
					return "pool is crt but the name claims another pool";
				}
				return null;
			case "binary_artifact":
				if (fdpsPrefix || ailPrefix) {
					return "pool is binary_artifact but the name claims another pool";
				}
				return null;
			default:
				return "unknown pool " + pool;
		}
	}

	/** Names Ghidra made up on its own, which carry no judgement. */
	private static boolean isAutoName(String name) {
		return name.startsWith("FUN_") || name.startsWith("SUB_")
			|| name.startsWith("thunk_FUN_") || name.startsWith("LAB_");
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}
}
