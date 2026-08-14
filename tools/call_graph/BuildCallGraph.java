// Dump the FDPS.LE call graph from Ghidra as JSON.
//
// Emits every function, every direct CALL/JUMP edge between functions, every
// function-pointer table found in memory, and the indirect edges those tables
// imply. Read-only: nothing is written back to the Ghidra database.
//
// Usage: run through the Ghidra MCP `run_ghidra_script` tool with an absolute
// path. Optional first argument overrides the output directory.
//
//@category FDPS

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

public class BuildCallGraph extends GhidraScript {

    /** Shortest run of consecutive function pointers that counts as a table. */
    private static final int MIN_TABLE_ENTRIES = 4;

    private static final String DEFAULT_OUT_DIR =
        "C:\\Users\\fdpsf\\Documents\\fdps-anatomy\\workspace\\call_graph";

    private FunctionManager fm;
    private ReferenceManager rm;
    private Memory mem;

    @Override
    protected void run() throws Exception {
        if (!"FDPS.LE".equals(currentProgram.getName())) {
            throw new IllegalStateException(
                "expected FDPS.LE to be the current program, got " + currentProgram.getName());
        }
        fm = currentProgram.getFunctionManager();
        rm = currentProgram.getReferenceManager();
        mem = currentProgram.getMemory();

        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 && !args[0].isEmpty() ? args[0] : DEFAULT_OUT_DIR);
        outDir.mkdirs();

        List<Function> functions = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) {
            functions.add(f);
        }
        Collections.sort(functions, (a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));

        List<String[]> directEdges = collectDirectEdges(functions);
        List<PointerRun> runs = findPointerRuns();
        List<String[]> indirectEdges = runsToEdges(runs);

        int dispatched = 0;
        for (PointerRun r : runs) {
            if (!r.viewBases.isEmpty()) {
                dispatched++;
            }
        }

        File out = new File(outDir, "graph.json");
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            writeJson(w, functions, directEdges, runs, indirectEdges);
        }

        println("functions=" + functions.size()
            + " direct_edges=" + directEdges.size()
            + " pointer_runs=" + runs.size()
            + " (dispatched=" + dispatched + ")"
            + " indirect_edges=" + indirectEdges.size());
        println("written: " + out.getAbsolutePath());
    }

    // ---------------------------------------------------------------- edges

    /**
     * Every CALL or JUMP reference whose target is the entry point of another
     * function. Jumps matter because the LE entry point is a bare thunk and
     * tail calls show up the same way.
     */
    private List<String[]> collectDirectEdges(List<Function> functions) {
        Set<String> seen = new LinkedHashSet<>();
        List<String[]> edges = new ArrayList<>();
        for (Function f : functions) {
            Address from = f.getEntryPoint();
            AddressSetView body = f.getBody();
            for (Address site : rm.getReferenceSourceIterator(body, true)) {
                for (Reference r : rm.getReferencesFrom(site)) {
                    RefType rt = r.getReferenceType();
                    boolean isCall = rt.isCall();
                    boolean isJump = rt.isJump();
                    if (!isCall && !isJump) {
                        continue;
                    }
                    Function target = fm.getFunctionAt(r.getToAddress());
                    if (target == null) {
                        continue;
                    }
                    Address to = target.getEntryPoint();
                    if (to.equals(from)) {
                        continue;
                    }
                    String kind = isCall ? "CALL" : "JUMP";
                    String key = from + ">" + to + ">" + kind;
                    if (seen.add(key)) {
                        edges.add(new String[] { from.toString(), to.toString(), kind, site.toString() });
                    }
                }
            }
        }
        return edges;
    }

    // ------------------------------------------------------- pointer tables

    /**
     * A maximal run of dword slots that all point into code, together with the
     * dispatch sites that read it.
     */
    private static class PointerRun {
        Address base;
        /** Owning function of each slot's target, in slot order. */
        List<Address> targets = new ArrayList<>();
        /** Per slot: does the raw value land exactly on a function entry point? */
        List<Boolean> exact = new ArrayList<>();
        /** Slot addresses that code references, i.e. the base of each view. */
        List<Address> viewBases = new ArrayList<>();
        /** view base -> functions that dispatch through it. */
        Map<Address, Set<Address>> viewCallers = new LinkedHashMap<>();
        /** view base -> referencing instruction addresses. */
        Map<Address, Set<String>> viewSites = new LinkedHashMap<>();
        /** view base -> mnemonics of the dispatching instructions (CALL or JMP). */
        Map<Address, Set<String>> viewMnemonics = new LinkedHashMap<>();
    }

    /**
     * Scan initialised memory for runs of dword-aligned values that point into
     * some function's body. Slots inside a function body are skipped so that
     * immediate operands in code cannot be mistaken for a table.
     *
     * Targeting the body rather than the entry point matters: the 80x87
     * emulator's opcode table points at labels inside one large function, and
     * requiring an exact entry point would truncate that table at its 49th slot.
     */
    private List<PointerRun> findPointerRuns() {
        List<PointerRun> runs = new ArrayList<>();
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized()) {
                continue;
            }
            Address start = block.getStart();
            long last = block.getEnd().getOffset();
            long addr = (start.getOffset() + 3) & ~3L;
            List<Address> current = new ArrayList<>();
            List<Boolean> exact = new ArrayList<>();
            Address runBase = null;
            while (addr + 3 <= last) {
                Address here = start.getNewAddress(addr);
                Address owner = pointerTargetAt(here);
                if (owner != null) {
                    if (current.isEmpty()) {
                        runBase = here;
                    }
                    current.add(owner);
                    exact.add(isExactEntry(here, owner));
                } else {
                    flushRun(runs, runBase, current, exact);
                    current = new ArrayList<>();
                    exact = new ArrayList<>();
                    runBase = null;
                }
                addr += 4;
            }
            flushRun(runs, runBase, current, exact);
        }
        for (PointerRun run : runs) {
            annotateViews(run);
        }
        return runs;
    }

    /** Entry point of the function whose body contains the pointer at {@code here}, or null. */
    private Address pointerTargetAt(Address here) {
        if (fm.getFunctionContaining(here) != null) {
            return null;
        }
        int value;
        try {
            value = mem.getInt(here);
        } catch (Exception e) {
            return null;
        }
        if (value == 0) {
            return null;
        }
        Address candidate;
        try {
            candidate = here.getNewAddress(value & 0xffffffffL);
        } catch (Exception e) {
            return null;
        }
        Function f = fm.getFunctionContaining(candidate);
        return f == null ? null : f.getEntryPoint();
    }

    /** True when the value stored at {@code here} is the entry point of {@code owner}. */
    private boolean isExactEntry(Address here, Address owner) {
        try {
            long value = mem.getInt(here) & 0xffffffffL;
            return owner.getOffset() == value;
        } catch (Exception e) {
            return false;
        }
    }

    private void flushRun(List<PointerRun> runs, Address runBase, List<Address> current,
            List<Boolean> exact) {
        if (runBase == null || current.size() < MIN_TABLE_ENTRIES) {
            return;
        }
        PointerRun run = new PointerRun();
        run.base = runBase;
        run.targets = current;
        run.exact = exact;
        runs.add(run);
    }

    /**
     * Find the code that reads a run. The dispatch form is
     * {@code CALL dword ptr [reg*4 + base]}, so Ghidra records a reference from
     * the dispatching instruction to the slot the base names. Every referenced
     * slot is the base of one view; the views tile the run.
     */
    private void annotateViews(PointerRun run) {
        long size = 4L * run.targets.size();
        for (long off = 0; off < size; off += 4) {
            Address slot = run.base.add(off);
            for (Reference r : rm.getReferencesTo(slot)) {
                Address site = r.getFromAddress();
                Function owner = fm.getFunctionContaining(site);
                if (owner == null) {
                    continue;
                }
                Instruction insn = getInstructionAt(site);
                if (insn == null) {
                    continue;
                }
                String mnem = insn.getMnemonicString().toUpperCase();
                if (!mnem.startsWith("CALL") && !mnem.startsWith("JMP")) {
                    continue;
                }
                if (!run.viewCallers.containsKey(slot)) {
                    run.viewBases.add(slot);
                    run.viewCallers.put(slot, new TreeSet<Address>());
                    run.viewSites.put(slot, new LinkedHashSet<String>());
                    run.viewMnemonics.put(slot, new TreeSet<String>());
                }
                run.viewCallers.get(slot).add(owner.getEntryPoint());
                run.viewSites.get(slot).add(site.toString());
                run.viewMnemonics.get(slot).add(mnem.startsWith("CALL") ? "CALL" : "JMP");
            }
        }
        Collections.sort(run.viewBases);
    }

    /**
     * One edge per (dispatcher, slot target). A view owns the slots from its own
     * base up to the next view's base, so the views of a run partition it and no
     * target is left without an inbound edge.
     */
    private List<String[]> runsToEdges(List<PointerRun> runs) {
        Set<String> seen = new LinkedHashSet<>();
        List<String[]> edges = new ArrayList<>();
        for (PointerRun run : runs) {
            for (int v = 0; v < run.viewBases.size(); v++) {
                Address viewBase = run.viewBases.get(v);
                int first = (int) (viewBase.subtract(run.base) / 4);
                int stop = v + 1 < run.viewBases.size()
                    ? (int) (run.viewBases.get(v + 1).subtract(run.base) / 4)
                    : run.targets.size();
                for (Address caller : run.viewCallers.get(viewBase)) {
                    for (int i = first; i < stop; i++) {
                        Address target = run.targets.get(i);
                        if (caller.equals(target)) {
                            continue;
                        }
                        String key = caller + ">" + target + ">" + viewBase;
                        if (seen.add(key)) {
                            edges.add(new String[] {
                                caller.toString(), target.toString(), "TABLE", viewBase.toString() });
                        }
                    }
                }
            }
        }
        return edges;
    }

    // ----------------------------------------------------------------- json

    private void writeJson(PrintWriter w, List<Function> functions, List<String[]> directEdges,
            List<PointerRun> runs, List<String[]> indirectEdges) {
        w.println("{");
        w.println("  \"program\": \"" + esc(currentProgram.getName()) + "\",");
        w.println("  \"entry_point\": \"00043298\",");

        w.println("  \"functions\": [");
        for (int i = 0; i < functions.size(); i++) {
            Function f = functions.get(i);
            String name = f.getName();
            w.print("    {\"addr\": \"" + f.getEntryPoint() + "\""
                + ", \"size\": " + f.getBody().getNumAddresses()
                + ", \"name\": \"" + esc(name) + "\""
                + ", \"default_name\": " + (name.startsWith("FUN_") ? "true" : "false")
                + ", \"cc\": \"" + esc(String.valueOf(f.getCallingConventionName())) + "\""
                + ", \"thunk\": " + f.isThunk()
                + "}");
            w.println(i + 1 < functions.size() ? "," : "");
        }
        w.println("  ],");

        w.println("  \"direct_edges\": [");
        for (int i = 0; i < directEdges.size(); i++) {
            String[] e = directEdges.get(i);
            w.print("    [\"" + e[0] + "\", \"" + e[1] + "\", \"" + e[2] + "\", \"" + e[3] + "\"]");
            w.println(i + 1 < directEdges.size() ? "," : "");
        }
        w.println("  ],");

        w.println("  \"pointer_runs\": [");
        for (int i = 0; i < runs.size(); i++) {
            PointerRun r = runs.get(i);
            w.println("    {\"base\": \"" + r.base + "\", \"slots\": " + r.targets.size() + ",");
            w.println("     \"targets\": [" + joinAddrs(r.targets) + "],");
            w.println("     \"exact_entry\": [" + joinBooleans(r.exact) + "],");
            w.print("     \"views\": [");
            for (int v = 0; v < r.viewBases.size(); v++) {
                Address vb = r.viewBases.get(v);
                int first = (int) (vb.subtract(r.base) / 4);
                int stop = v + 1 < r.viewBases.size()
                    ? (int) (r.viewBases.get(v + 1).subtract(r.base) / 4)
                    : r.targets.size();
                if (v > 0) {
                    w.print(", ");
                }
                w.print("{\"base\": \"" + vb + "\", \"first_slot\": " + first
                    + ", \"entries\": " + (stop - first)
                    + ", \"dispatch\": [" + joinStrings(new ArrayList<>(r.viewMnemonics.get(vb))) + "]"
                    + ", \"callers\": [" + joinAddrs(new ArrayList<>(r.viewCallers.get(vb))) + "]"
                    + ", \"sites\": [" + joinStrings(new ArrayList<>(r.viewSites.get(vb))) + "]}");
            }
            w.print("]}");
            w.println(i + 1 < runs.size() ? "," : "");
        }
        w.println("  ],");

        w.println("  \"indirect_edges\": [");
        for (int i = 0; i < indirectEdges.size(); i++) {
            String[] e = indirectEdges.get(i);
            w.print("    [\"" + e[0] + "\", \"" + e[1] + "\", \"" + e[2] + "\", \"" + e[3] + "\"]");
            w.println(i + 1 < indirectEdges.size() ? "," : "");
        }
        w.println("  ]");
        w.println("}");
    }

    private String joinAddrs(List<Address> addrs) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < addrs.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append('"').append(addrs.get(i)).append('"');
        }
        return sb.toString();
    }

    private String joinBooleans(List<Boolean> flags) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < flags.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append(flags.get(i) ? "true" : "false");
        }
        return sb.toString();
    }

    private String joinStrings(List<String> items) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < items.size(); i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append('"').append(esc(items.get(i))).append('"');
        }
        return sb.toString();
    }

    private static String esc(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
