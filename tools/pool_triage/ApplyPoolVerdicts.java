// Transcribe per-function pool verdicts into Ghidra.
//
// One agent read one function's assembly and decided which pool it belongs to;
// this script records that decision as a function tag, as a Pool section in the
// plate comment, and -- only where the library evidence produced a real symbol
// name -- as the function's name.
//
// It never overwrites a name or a plate comment written by an earlier ticket:
// a function that already carries a chosen name keeps it, and the pool section
// is appended to whatever plate comment is there.
//
// Usage: run_ghidra_script with two or more arguments:
//   <verdict dir> all           apply every verdict file in the directory
//   <verdict dir> <addr> ...    apply only these functions (8 hex digits)
//
// Verdict file <addr>.json:
//   {
//     "addr": "0004361a",
//     "pool": "fdps" | "crt" | "ail" | "binary_artifact" | "unknown",
//     "name": "crt___CHK",              // optional, library symbol based
//     "library": "CLIB3S.LIB(chk386)",  // optional, what the name came from
//     "role": "one English sentence",
//     "evidence": "why this pool",
//     "confidence": "high" | "medium" | "low",
//     "open_question": ""
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
import java.util.Map;
import java.util.TreeMap;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyPoolVerdicts extends GhidraScript {

	private static final String POOL_HEADING = "Pool:";

	private final List<String> problems = new ArrayList<>();
	private final Map<String, Integer> counts = new TreeMap<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 2) {
			println("ERR: usage: ApplyPoolVerdicts <verdict dir> all|<addr>...");
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

		int applied = 0;
		int renamed = 0;
		int tagged = 0;
		for (String key : wanted) {
			File f = new File(dir, key + ".json");
			if (!f.isFile()) {
				problems.add("NO VERDICT FILE " + key);
				continue;
			}
			JsonObject v;
			try {
				v = JsonParser.parseString(
					new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8))
					.getAsJsonObject();
			}
			catch (Exception e) {
				problems.add("UNPARSEABLE " + key + ": " + e.getMessage());
				continue;
			}
			Address at = addr(str(v, "addr") == null ? key : str(v, "addr"));
			Function fn = at == null ? null : getFunctionAt(at);
			if (fn == null) {
				problems.add("NO FUNCTION AT " + key);
				continue;
			}
			String pool = str(v, "pool");
			if (pool == null || pool.isEmpty()) {
				pool = "unknown";
			}
			counts.merge(pool, 1, Integer::sum);
			try {
				// A rescan may move a function between pools, so drop whatever
				// pool tag an earlier round left before adding this one -- two
				// pool tags on one function would corrupt every tag-based count.
				for (ghidra.program.model.listing.FunctionTag t : new ArrayList<>(fn.getTags())) {
					String tn = t.getName();
					if (tn.startsWith("pool_") && !tn.equals("pool_" + pool)) {
						fn.removeTag(tn);
						problems.add("REPLACED STALE TAG " + key + " " + tn + " -> pool_" + pool);
					}
				}
				if (!"unknown".equals(pool)) {
					fn.addTag("pool_" + pool);
					tagged++;
				}
				String name = str(v, "name");
				if (name != null && !name.isEmpty() && fn.getName().startsWith("FUN_")) {
					try {
						fn.setName(uniqueName(name, at), SourceType.USER_DEFINED);
						renamed++;
					}
					catch (Exception ex) {
						problems.add("RENAME FAILED " + key + " -> " + name + ": " + ex.getMessage());
					}
				}
				appendPoolSection(fn, at, v, pool);
				applied++;
			}
			catch (Exception e) {
				problems.add("FAILED " + key + ": " + e.getClass().getSimpleName() + " "
					+ e.getMessage());
			}
		}

		println("verdicts applied = " + applied + " of " + wanted.size());
		println("functions renamed = " + renamed);
		println("pool tags added = " + tagged);
		println("pool breakdown = " + counts);
		println("problems = " + problems.size());
		for (String p : problems) {
			println("  " + p);
		}
	}

	private void appendPoolSection(Function fn, Address at, JsonObject v, String pool) {
		StringBuilder section = new StringBuilder();
		section.append(POOL_HEADING).append(' ').append(pool);
		String library = str(v, "library");
		if (library != null && !library.isEmpty()) {
			section.append("  [").append(library).append(']');
		}
		section.append('\n');
		String role = str(v, "role");
		if (role != null && !role.isEmpty()) {
			section.append(role).append('\n');
		}
		String evidence = str(v, "evidence");
		if (evidence != null && !evidence.isEmpty()) {
			section.append("Evidence: ").append(evidence).append('\n');
		}
		String open = str(v, "open_question");
		if (open != null && !open.isEmpty()) {
			section.append("Open question: ").append(open).append('\n');
		}

		String existing = getPlateComment(at);
		if (existing == null || existing.isEmpty()) {
			setPlateComment(at, section.toString().trim());
			return;
		}
		int idx = existing.indexOf(POOL_HEADING);
		if (idx >= 0) {
			// Replace the pool section this script wrote on an earlier pass.
			setPlateComment(at, (existing.substring(0, idx) + section).trim());
			return;
		}
		setPlateComment(at, existing.trim() + "\n\n" + section.toString().trim());
	}

	private String uniqueName(String name, Address at) {
		if (getGlobalFunctions(name).isEmpty()) {
			return name;
		}
		problems.add("NAME TAKEN " + name + ", using " + name + "_" + at);
		return name + "_" + at;
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
