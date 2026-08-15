// Transcribe reviewed backbone-walk decisions into the Ghidra database.
//
// This script makes no judgements. Every name, prototype and plate comment it
// writes was decided one function at a time -- proposed by an agent that read
// only that function, then reviewed and where necessary overridden by the
// orchestrator (see workspace/backbone_walk/decisionsNN.json). The script
// exists so long plate comments reach Ghidra without being retyped.
//
// Usage: run through Ghidra MCP `run_ghidra_script` with the apply list as the
// first argument, e.g. workspace/backbone_walk/apply01.json
//
//@category FDPS

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

public class ApplyBackboneWalk extends GhidraScript {

    private static final String DEFAULT_LIST =
        "C:\\Users\\fdpsf\\Documents\\fdps-anatomy\\workspace\\backbone_walk\\apply01.json";

    @Override
    protected void run() throws Exception {
        if (!"FDPS.LE".equals(currentProgram.getName())) {
            throw new IllegalStateException(
                "expected FDPS.LE to be the current program, got " + currentProgram.getName());
        }
        String[] args = getScriptArgs();
        File listFile = new File(args.length > 0 && !args[0].isEmpty() ? args[0] : DEFAULT_LIST);
        String json = new String(Files.readAllBytes(listFile.toPath()), StandardCharsets.UTF_8);

        List<Item> items = parse(json);
        println("applying " + items.size() + " item(s) from " + listFile);

        int renamed = 0;
        int plated = 0;
        int noReturn = 0;
        List<String> failures = new ArrayList<>();

        for (Item item : items) {
            Address addr = currentProgram.getAddressFactory().getAddress(item.addr);
            Function f = getFunctionAt(addr);
            if (f == null) {
                failures.add(item.addr + ": no function at address");
                continue;
            }
            try {
                f.setName(item.name, SourceType.USER_DEFINED);
                renamed++;
            } catch (Exception e) {
                failures.add(item.addr + ": rename failed: " + e.getMessage());
            }
            try {
                setPlateComment(addr, item.plateComment);
                plated++;
            } catch (Exception e) {
                failures.add(item.addr + ": plate comment failed: " + e.getMessage());
            }
            if (item.noReturn && !f.hasNoReturn()) {
                f.setNoReturn(true);
                noReturn++;
            }
            println("  " + item.addr + "  " + item.name);
        }

        println("renamed=" + renamed + " plate_comments=" + plated + " no_return=" + noReturn);
        if (failures.isEmpty()) {
            println("no failures");
        } else {
            for (String failure : failures) {
                println("FAILURE " + failure);
            }
            throw new IllegalStateException(failures.size() + " item(s) failed");
        }
        println("NOTE: prototypes are set separately through the MCP "
            + "set_function_prototype tool, which validates the signature text.");
    }

    private static class Item {
        String addr;
        String name;
        String plateComment;
        boolean noReturn;
    }

    /**
     * Minimal reader for the flat array this pipeline produces. Only the shape
     * written by make_apply.py is supported: an array of objects whose values
     * are strings or booleans, with no nesting.
     */
    private List<Item> parse(String json) {
        List<Item> items = new ArrayList<>();
        int i = 0;
        while (true) {
            int open = json.indexOf('{', i);
            if (open < 0) {
                break;
            }
            int close = findObjectEnd(json, open);
            String body = json.substring(open, close + 1);
            Item item = new Item();
            item.addr = stringField(body, "addr");
            item.name = stringField(body, "name");
            item.plateComment = stringField(body, "plate_comment");
            item.noReturn = "true".equals(rawField(body, "no_return"));
            if (item.addr == null || item.name == null) {
                throw new IllegalStateException("malformed item near offset " + open);
            }
            items.add(item);
            i = close + 1;
        }
        return items;
    }

    private int findObjectEnd(String s, int open) {
        boolean inString = false;
        boolean escaped = false;
        int depth = 0;
        for (int i = open; i < s.length(); i++) {
            char c = s.charAt(i);
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    return i;
                }
            }
        }
        throw new IllegalStateException("unterminated object at offset " + open);
    }

    private String stringField(String body, String key) {
        int at = fieldValueStart(body, key);
        if (at < 0 || body.charAt(at) != '"') {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        boolean escaped = false;
        for (int i = at + 1; i < body.length(); i++) {
            char c = body.charAt(i);
            if (escaped) {
                switch (c) {
                    case 'n': sb.append('\n'); break;
                    case 't': sb.append('\t'); break;
                    case 'r': sb.append('\r'); break;
                    case 'u':
                        sb.append((char) Integer.parseInt(body.substring(i + 1, i + 5), 16));
                        i += 4;
                        break;
                    default: sb.append(c);
                }
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                return sb.toString();
            } else {
                sb.append(c);
            }
        }
        throw new IllegalStateException("unterminated string for key " + key);
    }

    private String rawField(String body, String key) {
        int at = fieldValueStart(body, key);
        if (at < 0) {
            return null;
        }
        int end = at;
        while (end < body.length() && ",}\n\r ".indexOf(body.charAt(end)) < 0) {
            end++;
        }
        return body.substring(at, end);
    }

    private int fieldValueStart(String body, String key) {
        int at = body.indexOf('"' + key + '"');
        if (at < 0) {
            return -1;
        }
        int colon = body.indexOf(':', at);
        int i = colon + 1;
        while (i < body.length() && Character.isWhitespace(body.charAt(i))) {
            i++;
        }
        return i;
    }
}
