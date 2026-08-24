// Counts the code shapes that separate Watcom 10.0 from 10.0a, per pool.
//
// compiler_diff.py measured the difference on probes: for a bit test on a
// byte in memory, 10.0 folds the operand into TEST byte ptr [mem],imm8 while
// 10.0a and 10.0b load the byte first and test the register.  That difference
// is in the compiler, not the runtime library, so counting the two shapes in
// FDPS.LE's own game code says something about the compiler that no amount of
// library comparison can.
//
// Only the game pool is evidence.  The AIL and CRT pools were compiled by
// someone else, at another time, with other settings, and their shapes say
// nothing about the build that produced this executable.
//
// Args:
//   outFile - path of the JSON to write (optional; prints either way)

import java.io.File;
import java.io.FileWriter;
import java.util.LinkedHashMap;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class CountCodeShapes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();

        // shape -> pool -> count
        Map<String, Map<String, Integer>> counts = new LinkedHashMap<String, Map<String, Integer>>();
        String[] shapes = {"test_mem8_imm", "load_then_test_reg", "test_al_imm"};
        for (String s : shapes) counts.put(s, new LinkedHashMap<String, Integer>());

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Instruction prev1 = null, prev2 = null, prev3 = null;
        while (it.hasNext()) {
            Instruction ins = it.next();
            byte[] b;
            try { b = ins.getBytes(); } catch (Exception e) { b = new byte[0]; }
            String pool = poolOf(ins);

            // TEST r/m8, imm8 -- opcode F6 with reg field 0, memory operand
            if (b.length >= 2 && (b[0] & 0xff) == 0xF6 && ((b[1] >> 3) & 0x07) == 0
                    && (b[1] & 0xC0) != 0xC0) {
                bump(counts.get("test_mem8_imm"), pool);
            }
            // TEST AL, imm8
            if (b.length >= 1 && (b[0] & 0xff) == 0xA8) {
                bump(counts.get("test_al_imm"), pool);
            }
            // AND EAX,0xff followed by TEST EAX,EAX, preceded by AND AL,imm8:
            // the 10.0a expansion of the same source construct
            if (b.length == 2 && (b[0] & 0xff) == 0x85 && (b[1] & 0xff) == 0xC0
                    && isAndEaxFF(prev1) && isAndAlImm(prev2) && isLoadAl(prev3)) {
                bump(counts.get("load_then_test_reg"), pool);
            }
            prev3 = prev2;
            prev2 = prev1;
            prev1 = ins;
        }

        StringBuilder sb = new StringBuilder("{\n");
        boolean firstShape = true;
        for (String shape : shapes) {
            if (!firstShape) sb.append(",\n");
            firstShape = false;
            sb.append("  \"").append(shape).append("\": {");
            boolean first = true;
            for (Map.Entry<String, Integer> e : counts.get(shape).entrySet()) {
                if (!first) sb.append(", ");
                first = false;
                sb.append("\"").append(e.getKey()).append("\": ").append(e.getValue());
            }
            sb.append("}");
            println(shape + ": " + counts.get(shape));
        }
        sb.append("\n}\n");

        if (argv.length > 0) {
            File out = new File(argv[0]);
            out.getParentFile().mkdirs();
            FileWriter w = new FileWriter(out);
            try { w.write(sb.toString()); } finally { w.close(); }
            println("wrote: " + out);
        }
        println("DONE");
    }

    private boolean isAndEaxFF(Instruction ins) {
        if (ins == null) return false;
        byte[] b;
        try { b = ins.getBytes(); } catch (Exception e) { return false; }
        return b.length == 5 && (b[0] & 0xff) == 0x25 && (b[1] & 0xff) == 0xFF
                && b[2] == 0 && b[3] == 0 && b[4] == 0;
    }

    private boolean isAndAlImm(Instruction ins) {
        if (ins == null) return false;
        byte[] b;
        try { b = ins.getBytes(); } catch (Exception e) { return false; }
        return b.length == 2 && (b[0] & 0xff) == 0x24;
    }

    private boolean isLoadAl(Instruction ins) {
        if (ins == null) return false;
        byte[] b;
        try { b = ins.getBytes(); } catch (Exception e) { return false; }
        int op = b.length > 0 ? (b[0] & 0xff) : 0;
        return op == 0x8A || op == 0xA0;
    }

    private void bump(Map<String, Integer> m, String key) {
        Integer v = m.get(key);
        m.put(key, v == null ? 1 : v.intValue() + 1);
    }

    private String poolOf(Instruction ins) {
        Function fn = currentProgram.getFunctionManager().getFunctionContaining(ins.getAddress());
        if (fn == null) return "no_function";
        for (Object t : fn.getTags()) {
            String name = ((ghidra.program.model.listing.FunctionTag) t).getName();
            if (name.startsWith("pool_")) return name.substring(5);
        }
        return "untagged";
    }
}
