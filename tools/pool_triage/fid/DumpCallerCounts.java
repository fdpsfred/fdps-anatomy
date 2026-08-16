// Dumps, for every function in the current program, how many call references
// reach it from outside its own body. Reading it together with the Function ID
// result answers the question a bare hit rate cannot: an AIL function with no
// library counterpart only threatens the rebuild if something actually calls
// it. Linker-pulled dead code does not.
//
// Args:
//   outFile - filesystem path for the JSON output

import java.io.File;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

public class DumpCallerCounts extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        if (argv.length < 1) {
            println("ERR: need 1 arg: outFile");
            return;
        }
        File out = new File(argv[0]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();

        ReferenceManager rm = currentProgram.getReferenceManager();
        // UTF-8 explicitly: the readers on the Python side open with
        // encoding="utf-8", and this project allows non-ASCII identifiers for
        // in-game proper nouns, so the platform default (cp950 here) would
        // eventually produce a file they cannot decode.
        Writer w = new OutputStreamWriter(Files.newOutputStream(out.toPath()),
                StandardCharsets.UTF_8);
        int total = 0, dead = 0;
        try {
            w.write("{\n  \"program\": " + jsonString(currentProgram.getName())
                    + ",\n  \"functions\": [\n");
            boolean first = true;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                Address entry = f.getEntryPoint();
                int external = 0, internal = 0;
                for (Reference r : rm.getReferencesTo(entry)) {
                    if (f.getBody().contains(r.getFromAddress())) internal++; else external++;
                }
                if (external == 0) dead++;
                total++;
                if (!first) w.write(",\n");
                first = false;
                w.write("    {\"address\":" + jsonString(entry.toString())
                        + ",\"name\":" + jsonString(f.getName())
                        + ",\"external_refs\":" + external + ",\"self_refs\":" + internal + "}");
            }
            w.write("\n  ]\n}\n");
        } finally {
            w.close();
        }
        println("functions=" + total + " with no external reference=" + dead);
        println("wrote: " + out.getAbsolutePath());
        println("DONE");
    }

    private String jsonString(String s) {
        if (s == null) return "null";
        StringBuilder sb = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"') sb.append("\\\"");
            else if (c == '\\') sb.append("\\\\");
            else if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
            else sb.append(c);
        }
        sb.append("\"");
        return sb.toString();
    }
}
