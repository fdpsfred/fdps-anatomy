// Make every function's pool_* tag set match its verdict file exactly.
//
// ApplyPoolVerdicts adds pool_<pool> but never removes a pool_* tag that an
// earlier round wrote, so a function whose pool was corrected during the
// rescan ends up carrying both the old and the new tag. Any consumer that
// treats "tagged pool_fdps" as the game-code worklist then over-counts.
//
// This script is the repair and the guard: for each verdict it removes every
// pool_* tag other than the one the verdict names, adds the named one if it is
// missing, and reports what it changed. It also reports functions that carry a
// pool_* tag but have no verdict file at all.
//
// Usage: run_ghidra_script with one argument:
//   <verdict dir>       reconcile every verdict file in the directory
//
// Verdict file <addr>.json: see ApplyPoolVerdicts.java for the full shape; only
// the "pool" field is read here.
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;

public class ReconcilePoolTags extends GhidraScript {

	private static final String PREFIX = "pool_";

	private final List<String> problems = new ArrayList<>();
	private final Map<String, Integer> finalCounts = new TreeMap<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: ReconcilePoolTags <verdict dir>");
			return;
		}
		File dir = new File(argv[0]);
		File[] files = dir.listFiles((d, n) -> n.endsWith(".json"));
		if (files == null) {
			println("ERR: not a directory: " + dir);
			return;
		}
		Arrays.sort(files);

		Map<String, String> wantByAddr = new HashMap<>();
		for (File f : files) {
			String key = f.getName().replace(".json", "");
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
			String addr = str(v, "addr");
			String pool = str(v, "pool");
			if (pool == null || pool.isEmpty()) {
				pool = "unknown";
			}
			wantByAddr.put(addr == null || addr.isEmpty() ? key : addr, pool);
		}

		int checked = 0;
		int removed = 0;
		int added = 0;
		int changedFns = 0;
		Set<String> seen = new HashSet<>();

		for (Map.Entry<String, String> e : new TreeMap<>(wantByAddr).entrySet()) {
			String key = e.getKey();
			String pool = e.getValue();
			Address at = addr(key);
			Function fn = at == null ? null : getFunctionAt(at);
			if (fn == null) {
				problems.add("NO FUNCTION AT " + key);
				continue;
			}
			checked++;
			seen.add(key);
			String want = "unknown".equals(pool) ? null : PREFIX + pool;
			boolean touched = false;
			boolean hasWanted = false;
			for (FunctionTag t : new ArrayList<>(fn.getTags())) {
				String name = t.getName();
				if (!name.startsWith(PREFIX)) {
					continue;
				}
				if (want != null && name.equals(want)) {
					hasWanted = true;
					continue;
				}
				fn.removeTag(name);
				removed++;
				touched = true;
				println("REMOVE " + key + " " + name + " (verdict says " + pool + ")");
			}
			if (want != null && !hasWanted) {
				fn.addTag(want);
				added++;
				touched = true;
				println("ADD    " + key + " " + want);
			}
			if (touched) {
				changedFns++;
			}
			if (want != null) {
				finalCounts.merge(pool, 1, Integer::sum);
			}
		}

		// Any function carrying a pool tag with no verdict backing it at all.
		int orphanTagged = 0;
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			Function fn = it.next();
			String key = pad(fn.getEntryPoint());
			if (seen.contains(key)) {
				continue;
			}
			for (FunctionTag t : fn.getTags()) {
				if (t.getName().startsWith(PREFIX)) {
					orphanTagged++;
					problems.add("TAGGED WITHOUT VERDICT " + key + " " + t.getName());
				}
			}
		}

		println("verdicts checked = " + checked + " of " + wantByAddr.size());
		println("functions changed = " + changedFns);
		println("stale tags removed = " + removed);
		println("missing tags added = " + added);
		println("tagged without verdict = " + orphanTagged);
		println("final pool breakdown = " + finalCounts);
		println("problems = " + problems.size());
		for (String p : problems) {
			println("  " + p);
		}
	}

	private String pad(Address a) {
		String s = Long.toHexString(a.getOffset());
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private Address addr(String hex) {
		try {
			return currentProgram.getAddressFactory().getDefaultAddressSpace()
				.getAddress(Long.parseLong(hex.trim(), 16));
		}
		catch (Exception e) {
			return null;
		}
	}

	private String str(JsonObject o, String key) {
		JsonElement el = o.get(key);
		return el == null || el.isJsonNull() ? null : el.getAsString();
	}
}
