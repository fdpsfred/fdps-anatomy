// Dumps every CRT function's body bytes and its relocatable byte positions so
// the version sweep can compare them against the shipped Watcom libraries.
//
// A library comparison has to ignore the bytes the linker patched: absolute
// addresses and cross-object rel32 displacements differ between the .obj and
// the linked image even when the code is identical.  Every such position is
// marked here, once from the instruction's own references and once from a scan
// for any 4-byte little-endian value that lands inside the image, so that a
// reference Ghidra failed to record still gets masked.  Over-masking only
// shortens the comparable runs; under-masking would fake a version difference.
//
// Args:
//   outFile   - path of the JSON to write
//   tag       - function tag to select (default "pool_crt")

import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class DumpCrtBodies extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        if (argv.length < 1) {
            println("ERR: need args: outFile [tag]");
            return;
        }
        File out = new File(argv[0]);
        String wantTag = argv.length > 1 ? argv[1] : "pool_crt";
        out.getParentFile().mkdirs();

        long imageMin = currentProgram.getMinAddress().getOffset();
        long imageMax = currentProgram.getMaxAddress().getOffset();

        StringBuilder sb = new StringBuilder();
        sb.append("{\n  \"tag\": ").append(jsonString(wantTag)).append(",\n");
        sb.append("  \"functions\": [\n");

        int count = 0;
        FunctionIterator fns = currentProgram.getFunctionManager().getFunctions(true);
        boolean first = true;
        while (fns.hasNext()) {
            Function fn = fns.next();
            if (!hasTag(fn, wantTag)) continue;
            count++;
            if (!first) sb.append(",\n");
            first = false;
            sb.append(dumpFunction(fn, imageMin, imageMax));
        }
        sb.append("\n  ]\n}\n");

        FileWriter w = new FileWriter(out);
        try { w.write(sb.toString()); } finally { w.close(); }
        println("functions=" + count);
        println("wrote: " + out);
        println("DONE");
    }

    private boolean hasTag(Function fn, String tag) {
        for (Object t : fn.getTags()) {
            if (tag.equals(((ghidra.program.model.listing.FunctionTag) t).getName())) return true;
        }
        return false;
    }

    private String dumpFunction(Function fn, long imageMin, long imageMax) throws Exception {
        AddressSetView body = fn.getBody();
        Address entry = fn.getEntryPoint();
        long size = body.getNumAddresses();
        Address bodyMin = body.getMinAddress();
        Address bodyMax = body.getMaxAddress();

        // Only the range holding the entry point is dumped.  A body with holes
        // has another function's code sitting in the gaps, and splicing that in
        // would make the function match nothing anywhere; the remaining ranges
        // are listed so the reader can see what was left out.
        List<String> ranges = new ArrayList<String>();
        int len = 0;
        for (ghidra.program.model.address.AddressRange r : body) {
            ranges.add(r.getMinAddress().toString() + "-" + r.getMaxAddress().toString());
            if (r.contains(entry)) {
                len = (int) (r.getMaxAddress().getOffset() - entry.getOffset() + 1);
            }
        }
        if (len <= 0 || len > 0x20000) len = (int) Math.min(size, 0x20000);

        byte[] bytes = new byte[len];
        int got = 0;
        try { got = currentProgram.getMemory().getBytes(entry, bytes); }
        catch (Exception e) { got = 0; }
        if (got < len) len = Math.max(got, 0);

        TreeSet<Integer> masked = new TreeSet<Integer>();
        List<String> listing = new ArrayList<String>();
        InstructionIterator insns = currentProgram.getListing().getInstructions(body, true);
        while (insns.hasNext()) {
            Instruction ins = insns.next();
            int base = (int) (ins.getAddress().getOffset() - entry.getOffset());
            if (base < 0 || base >= len) continue;
            byte[] ib;
            try { ib = ins.getBytes(); } catch (Exception e) { continue; }
            listing.add(ins.getAddress().toString() + "  " + ins.toString());

            List<Long> wanted = new ArrayList<Long>();
            for (Reference ref : ins.getReferencesFrom()) {
                if (ref.getReferenceType().isFallthrough()) continue;
                long t = ref.getToAddress().getOffset();
                wanted.add(t);                                        // absolute
                wanted.add(t - (ins.getAddress().getOffset() + ib.length)); // rel32
            }
            for (int off = 1; off + 4 <= ib.length; off++) {
                long v = ((long) (ib[off] & 0xff))
                       | ((long) (ib[off + 1] & 0xff) << 8)
                       | ((long) (ib[off + 2] & 0xff) << 16)
                       | ((long) (ib[off + 3] & 0xff) << 24);
                boolean hit = false;
                for (Long t : wanted) {
                    if ((t.longValue() & 0xffffffffL) == v) { hit = true; break; }
                }
                // Any word that names a location inside the image is linker
                // output whether or not Ghidra recorded a reference for it.
                if (!hit && v >= imageMin && v <= imageMax) hit = true;
                if (hit) {
                    for (int k = 0; k < 4; k++) {
                        int p = base + off + k;
                        if (p >= 0 && p < len) masked.add(Integer.valueOf(p));
                    }
                }
            }
        }

        StringBuilder sb = new StringBuilder();
        sb.append("    {\n");
        sb.append("      \"address\": ").append(jsonString(entry.toString())).append(",\n");
        sb.append("      \"name\": ").append(jsonString(fn.getName())).append(",\n");
        sb.append("      \"body_size\": ").append(size).append(",\n");
        sb.append("      \"dump_len\": ").append(len).append(",\n");
        sb.append("      \"contiguous\": ").append(size == len).append(",\n");
        sb.append("      \"body_min\": ").append(jsonString(bodyMin.toString())).append(",\n");
        sb.append("      \"body_max\": ").append(jsonString(bodyMax.toString())).append(",\n");
        sb.append("      \"ranges\": [");
        for (int i = 0; i < ranges.size(); i++) {
            if (i > 0) sb.append(",");
            sb.append(jsonString(ranges.get(i)));
        }
        sb.append("],\n");
        sb.append("      \"calling_convention\": ").append(jsonString(fn.getCallingConventionName())).append(",\n");
        sb.append("      \"plate\": ").append(jsonString(firstLineOfPlate(fn))).append(",\n");
        sb.append("      \"bytes\": ").append(jsonString(hex(bytes, len))).append(",\n");
        sb.append("      \"masked\": [");
        boolean f2 = true;
        for (Iterator<Integer> it = masked.iterator(); it.hasNext(); ) {
            if (!f2) sb.append(",");
            f2 = false;
            sb.append(it.next().toString());
        }
        sb.append("],\n");
        sb.append("      \"listing\": [");
        for (int i = 0; i < listing.size(); i++) {
            if (i > 0) sb.append(",");
            sb.append(jsonString(listing.get(i)));
        }
        sb.append("]\n");
        sb.append("    }");
        return sb.toString();
    }

    private String firstLineOfPlate(Function fn) {
        String c = currentProgram.getListing().getComment(CodeUnit.PLATE_COMMENT, fn.getEntryPoint());
        if (c == null) return "";
        int nl = c.indexOf('\n');
        return nl < 0 ? c : c.substring(0, nl);
    }

    private String hex(byte[] b, int len) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < len; i++) sb.append(String.format("%02x", b[i] & 0xff));
        return sb.toString();
    }

    private String jsonString(String s) {
        if (s == null) return "\"\"";
        StringBuilder sb = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"') sb.append("\\\"");
            else if (c == '\\') sb.append("\\\\");
            else if (c == '\n') sb.append("\\n");
            else if (c == '\r') sb.append("\\r");
            else if (c == '\t') sb.append("\\t");
            else if (c < 0x20 || c > 0x7e) sb.append(String.format("\\u%04x", (int) c));
            else sb.append(c);
        }
        return sb.append("\"").toString();
    }
}
