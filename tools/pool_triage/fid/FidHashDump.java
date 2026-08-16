// Dumps, for every function in the current program, the Function ID hash quad
// and the code-unit extent FID measured. A function whose extent is shorter
// than FidService.SHORT_HASH_CODE_UNIT_LENGTH (4) has no hash at all and can
// never match anything, so this separates "no library counterpart" from "too
// short to ask the question" when reading a query result.
//
// Run it against FDPS.LE and against an imported library module; comparing the
// full hashes of the two dumps answers "is this the same code" without going
// through the scorer.
//
// Args:
//   outFile - filesystem path for the JSON output

import java.io.File;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.hash.FidHashQuad;
import ghidra.feature.fid.service.FidService;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.FunctionTag;

public class FidHashDump extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        if (argv.length < 1) {
            println("ERR: need 1 arg: outFile");
            return;
        }
        File out = new File(argv[0]);
        if (out.getParentFile() != null) out.getParentFile().mkdirs();

        FidService service = new FidService();
        FunctionManager fm = currentProgram.getFunctionManager();

        int total = 0, hashed = 0, tooShort = 0;
        // UTF-8 explicitly, not the platform default: the Python readers open
        // this file with encoding="utf-8".
        Writer w = new OutputStreamWriter(Files.newOutputStream(out.toPath()),
                StandardCharsets.UTF_8);
        try {
            w.write("{\n");
            w.write("  \"program\": " + jsonString(currentProgram.getName()) + ",\n");
            w.write("  \"functions\": [\n");
            boolean first = true;
            for (Function f : fm.getFunctions(true)) {
                total++;
                FidHashQuad quad = null;
                try {
                    quad = service.hashFunction(f);
                } catch (Throwable t) {
                    // A function whose body reaches uninitialised memory cannot be
                    // hashed; that is a fact about the function, not an error here.
                }
                if (quad == null) tooShort++; else hashed++;

                StringBuilder tags = new StringBuilder();
                for (FunctionTag tag : f.getTags()) {
                    if (tags.length() > 0) tags.append(",");
                    tags.append(tag.getName());
                }

                if (!first) w.write(",\n");
                first = false;
                w.write("    {\"address\":" + jsonString(f.getEntryPoint().toString()));
                w.write(",\"name\":" + jsonString(f.getName()));
                w.write(",\"body_size\":" + f.getBody().getNumAddresses());
                w.write(",\"tags\":" + jsonString(tags.toString()));
                if (quad == null) {
                    w.write(",\"hashable\":false}");
                } else {
                    w.write(",\"hashable\":true");
                    w.write(",\"code_units\":" + quad.getCodeUnitSize());
                    w.write(",\"full_hash\":" + jsonString(Long.toHexString(quad.getFullHash())));
                    w.write(",\"specific_hash\":" + jsonString(Long.toHexString(quad.getSpecificHash())));
                    w.write(",\"specific_hash_units\":" + quad.getSpecificHashAdditionalSize() + "}");
                }
            }
            w.write("\n  ]\n}\n");
        } finally {
            w.close();
        }
        println("functions=" + total + " hashable=" + hashed + " tooShort=" + tooShort);
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
