// Renames vendor-pool functions to the symbol their library uses, from the map
// build_symbol_renames.py produced. Transcription only: every name in the map is
// backed by evidence recorded elsewhere, and this script refuses anything it
// cannot verify rather than guessing.
//
// It renames in two passes. A rename map routinely contains swaps - the public
// entry point and its inner worker holding each other's names is the normal
// case, not an exotic one - so every affected function first takes a unique
// placeholder and only then its final name. Renaming in one pass would make
// Ghidra reject the second half of every swap, or silently append a suffix.
//
// Args:
//   mapPath   - mechanical.json from build_symbol_renames.py
//   apply     - "apply" to write; anything else lists what would change

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyLibrarySymbolNames extends GhidraScript {

    @Override
    public void run() throws Exception {
        if (!"FDPS.LE".equals(currentProgram.getName())) {
            throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
        }
        String[] argv = getScriptArgs();
        if (argv.length < 1) {
            println("ERR: need at least 1 arg: mapPath [apply]");
            return;
        }
        boolean write = argv.length >= 2 && "apply".equals(argv[1]);

        String text = new String(Files.readAllBytes(new File(argv[0]).toPath()),
                StandardCharsets.UTF_8);
        JsonObject map = JsonParser.parseString(text).getAsJsonObject();

        List<Address> addrs = new ArrayList<>();
        List<String> targets = new ArrayList<>();
        List<String> problems = new ArrayList<>();

        for (Map.Entry<String, com.google.gson.JsonElement> e : map.entrySet()) {
            Address a = currentProgram.getAddressFactory().getAddress(e.getKey());
            JsonObject v = e.getValue().getAsJsonObject();
            String from = v.get("from").getAsString();
            String to = v.get("to").getAsString();
            Function f = a == null ? null : getFunctionAt(a);
            if (f == null) {
                problems.add(e.getKey() + ": no function there");
                continue;
            }
            if (!f.getName().equals(from)) {
                problems.add(e.getKey() + ": expected to be named " + from + " but it is "
                        + f.getName() + "; the map is stale, not applying");
                continue;
            }
            addrs.add(a);
            targets.add(to);
        }

        println("renames to apply: " + addrs.size() + "  refused: " + problems.size());
        for (String p : problems) println("  PROBLEM " + p);
        if (!write) {
            println("dry run; pass \"apply\" as the second argument to write");
            println("DONE");
            return;
        }

        int tx = currentProgram.startTransaction("apply library symbol names");
        boolean ok = false;
        int renamed = 0;
        try {
            for (int i = 0; i < addrs.size(); i++) {
                getFunctionAt(addrs.get(i)).setName("__rn_" + addrs.get(i), SourceType.USER_DEFINED);
            }
            for (int i = 0; i < addrs.size(); i++) {
                Function f = getFunctionAt(addrs.get(i));
                f.setName(targets.get(i), SourceType.USER_DEFINED);
                if (!f.getName().equals(targets.get(i))) {
                    problems.add(addrs.get(i) + ": asked for " + targets.get(i)
                            + " but ended up as " + f.getName());
                } else {
                    renamed++;
                }
            }
            // All or nothing. Pass one parks every function under a __rn_
            // placeholder, so committing a partial pass two would leave real
            // functions named __rn_<addr> - and the next run refuses them,
            // because their name no longer matches the map. Rolling back puts
            // the program exactly where it started instead.
            ok = problems.isEmpty();
        } finally {
            currentProgram.endTransaction(tx, ok);
        }
        if (!ok) {
            println("ERR: rolled back; nothing was renamed");
        }

        println("renamed: " + renamed + "/" + addrs.size());
        for (String p : problems) println("  PROBLEM " + p);
        println(problems.isEmpty() ? "DONE" : "DONE WITH PROBLEMS");
    }
}
