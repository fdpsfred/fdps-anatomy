// Tag walked functions with their pool and subsystem, and tag the shared
// helper clusters the call graph identified.
//
// Ticket 12 asks for the shared helper clusters to be marked so that later
// partitioning keeps each cluster whole. Function tags are the durable place
// for that: they survive in the Ghidra snapshot and can be queried.
//
// Pool and subsystem come from the per-function verdicts, one judgement per
// function. The shared-helper tag is a whole-graph fact -- a caller count --
// and is applied by threshold, not by judging any function individually.
//
// Usage: run through Ghidra MCP `run_ghidra_script` with two arguments:
//   <index.json from collect_verdicts.py> <graph.json from BuildCallGraph>
//
//@category FDPS

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

public class TagBackbone extends GhidraScript {

    /** Matches SHARED_HELPER_MIN_CALLERS in analyze_graph.py. */
    private static final int SHARED_HELPER_MIN_CALLERS = 8;

    @Override
    protected void run() throws Exception {
        if (!"FDPS.LE".equals(currentProgram.getName())) {
            throw new IllegalStateException(
                "expected FDPS.LE to be the current program, got " + currentProgram.getName());
        }
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalStateException("usage: TagBackbone <index.json> <graph.json>");
        }
        String index = read(args[0]);
        String graph = read(args[1]);

        int poolTags = 0;
        int subsystemTags = 0;
        int backboneTags = 0;
        Map<String, Integer> poolCounts = new TreeMap<>();

        for (String obj : objects(index)) {
            String addr = field(obj, "addr");
            String pool = field(obj, "pool");
            String subsystem = field(obj, "subsystem");
            if (addr == null) {
                continue;
            }
            Function f = getFunctionAt(toAddress(addr));
            if (f == null) {
                println("WARN no function at " + addr);
                continue;
            }
            f.addTag("backbone");
            backboneTags++;
            if (pool != null && !pool.isEmpty() && !"unknown".equals(pool)) {
                f.addTag("pool_" + pool);
                poolTags++;
                poolCounts.merge(pool, 1, Integer::sum);
            }
            if (subsystem != null && !subsystem.isEmpty() && !"unknown".equals(subsystem)) {
                f.addTag("subsys_" + subsystem);
                subsystemTags++;
            }
        }

        // Shared helpers: count distinct callers over the direct edges.
        Map<String, Set<String>> callers = new HashMap<>();
        for (String edge : arrayOfArrays(graph, "direct_edges")) {
            String[] parts = edge.split("\",\\s*\"");
            if (parts.length < 2) {
                continue;
            }
            String from = parts[0].replace("\"", "").replace("[", "").trim();
            String to = parts[1].replace("\"", "").trim();
            callers.computeIfAbsent(to, k -> new HashSet<String>()).add(from);
        }
        int sharedTags = 0;
        for (Map.Entry<String, Set<String>> e : callers.entrySet()) {
            if (e.getValue().size() < SHARED_HELPER_MIN_CALLERS) {
                continue;
            }
            Function f = getFunctionAt(toAddress(e.getKey()));
            if (f == null) {
                continue;
            }
            f.addTag("shared_helper");
            sharedTags++;
        }

        println("backbone=" + backboneTags + " pool=" + poolTags
            + " subsystem=" + subsystemTags + " shared_helper=" + sharedTags);
        println("pool breakdown = " + poolCounts);
    }

    private Address toAddress(String hex) {
        return currentProgram.getAddressFactory().getAddress(hex);
    }

    private String read(String path) throws Exception {
        return new String(Files.readAllBytes(new File(path).toPath()), StandardCharsets.UTF_8);
    }

    /** Split a flat JSON array of objects into its object bodies. */
    private java.util.List<String> objects(String json) {
        java.util.List<String> out = new java.util.ArrayList<>();
        int i = 0;
        while (true) {
            int open = json.indexOf('{', i);
            if (open < 0) {
                break;
            }
            int close = json.indexOf('}', open);
            if (close < 0) {
                break;
            }
            out.add(json.substring(open, close + 1));
            i = close + 1;
        }
        return out;
    }

    /** Pull one array-of-arrays section out of the call graph dump. */
    private java.util.List<String> arrayOfArrays(String json, String key) {
        java.util.List<String> out = new java.util.ArrayList<>();
        int at = json.indexOf('"' + key + '"');
        if (at < 0) {
            return out;
        }
        int start = json.indexOf('[', at);
        int depth = 0;
        int i = start;
        int itemStart = -1;
        for (; i < json.length(); i++) {
            char c = json.charAt(i);
            if (c == '[') {
                depth++;
                if (depth == 2) {
                    itemStart = i;
                }
            } else if (c == ']') {
                depth--;
                if (depth == 1 && itemStart >= 0) {
                    out.add(json.substring(itemStart + 1, i));
                    itemStart = -1;
                } else if (depth == 0) {
                    break;
                }
            }
        }
        return out;
    }

    private String field(String body, String key) {
        int at = body.indexOf('"' + key + '"');
        if (at < 0) {
            return null;
        }
        int q1 = body.indexOf('"', body.indexOf(':', at) + 1);
        if (q1 < 0) {
            return null;
        }
        int q2 = body.indexOf('"', q1 + 1);
        return q2 < 0 ? null : body.substring(q1 + 1, q2);
    }
}
