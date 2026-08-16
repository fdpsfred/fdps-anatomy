// Repairs the function boundaries of an imported FD2 AIL module before it is
// turned into a .fidb.
//
// FD2's OMF emitter publishes a symbol for every mid-function alternate entry
// it had to reference, named `L_<function>_alt_<hex offset>`. Those are jump
// targets inside a function, not functions. Ghidra's auto-analysis takes each
// one as a function start, which truncates the body of the real function that
// contains it - and a truncated body hashes differently from the same code in
// FDPS.LE, so the affected functions silently fail to match.
//
// So every alternate entry is demoted to a plain label and the real functions
// are rebuilt to span the code again. Rebuilding runs in descending address
// order: the following function already exists when its predecessor is created,
// which is what stops flow-following from swallowing it.
//
// Demoting all of them goes too far in one direction. A few of these labels are
// entered only by a jump from a *different* function, so no flow from the
// containing function's entry reaches them and their code ends up in no
// function at all. Those get restored: after the rebuild, any alternate entry
// that starts a stretch of uncovered code becomes a function again. The rule is
// therefore "merge the entries the containing function already reaches, keep
// the ones nothing else does", and the outcome is checked by coverage, not
// assumed.
//
// Re-runnable: it works from the labels, not from the current function set, so
// running it twice lands on the same boundaries.
//
// Args:
//   programPath - project path of the module (e.g. /ail_lib/ail_code.obj)

import java.util.ArrayList;
import java.util.List;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class FidDemoteAltEntries extends GhidraScript {

    private static final String ALT_PREFIX = "L_";
    private static final String CODE_BLOCK = "_TEXT";

    // What the OMF loader marks a PUBDEF with, plus whatever a previous run of
    // this script created. Everything else in the block is Ghidra's own doing.
    private static final java.util.Set<SourceType> ENTRY_SOURCES =
            java.util.EnumSet.of(SourceType.IMPORTED, SourceType.USER_DEFINED);

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        if (argv.length < 1) {
            println("ERR: need 1 arg: programPath");
            return;
        }
        DomainFile df = lookup(argv[0]);
        if (df == null) {
            println("ERR: file not found: " + argv[0]);
            return;
        }
        Program p = (Program) df.getDomainObject(this, true, false, monitor);
        try {
            int tx = p.startTransaction("demote alt entries");
            boolean ok = false;
            try {
                ok = rebuild(p);
                p.endTransaction(tx, ok);
            } catch (Throwable t) {
                p.endTransaction(tx, false);
                throw t;
            }
            if (!ok) {
                println("ERR: rebuild failed, changes rolled back");
                return;
            }
            if (p.isChanged()) {
                df.save(monitor);
                println("saved " + df.getPathname());
            }
        } finally {
            p.release(this);
        }
        println("DONE");
    }

    private boolean rebuild(Program p) throws Exception {
        FunctionManager fm = p.getFunctionManager();
        SymbolTable st = p.getSymbolTable();
        MemoryBlock code = p.getMemory().getBlock(CODE_BLOCK);
        if (code == null) {
            println("ERR: no " + CODE_BLOCK + " block");
            return false;
        }
        AddressSet blockSet = new AddressSet(code.getStart(), code.getEnd());

        // Every symbol the module publishes inside _TEXT is an entry point: the
        // OMF module carries one PUBDEF per function plus the alternate-entry
        // labels, nothing else. Working from symbols rather than from the
        // current function set keeps the script re-runnable and recovers entry
        // points that an earlier pass dropped (deleting a function silently
        // takes any thunk pointing at it with it).
        List<Address> altAddrs = new ArrayList<>();
        List<String> altNames = new ArrayList<>();
        List<Address> real = new ArrayList<>();
        List<String> realNames = new ArrayList<>();
        for (Symbol s : st.getSymbolIterator(true)) {
            Address a = s.getAddress();
            if (!blockSet.contains(a)) continue;
            if (!s.isPrimary()) continue;
            // Only symbols the module itself published. Ghidra's own analysis
            // invents labels inside function bodies (switch tables, jump
            // targets) that do not carry the L_ prefix, and treating one of
            // those as an entry point would truncate its containing function -
            // reintroducing exactly the boundary mismatch this script removes.
            if (!ENTRY_SOURCES.contains(s.getSource())) {
                println("  ignoring non-module symbol " + a + " " + s.getName()
                        + " (" + s.getSource() + ")");
                continue;
            }
            if (s.getName().startsWith(ALT_PREFIX)) {
                altAddrs.add(a);
                altNames.add(s.getName());
            } else {
                real.add(a);
                realNames.add(s.getName());
            }
        }
        println("alt-entry labels: " + altAddrs.size() + "  real entry points: " + real.size());

        // Thunks are removed implicitly when the function they point at goes, so
        // the whole set is deleted and rebuilt rather than deleted selectively.
        for (Address a : altAddrs) {
            if (fm.getFunctionAt(a) != null) fm.removeFunction(a);
        }
        for (Address a : real) {
            if (fm.getFunctionAt(a) != null) fm.removeFunction(a);
        }

        int created = 0, failed = 0;
        for (int i = real.size() - 1; i >= 0; i--) {
            if (create(p, real.get(i), realNames.get(i))) created++; else failed++;
        }
        println("recreated real functions: " + created + "  failed: " + failed);

        // Restore the alternate entries whose code nothing else covers.
        int restored = 0;
        for (int pass = 0; pass < 8; pass++) {
            AddressSet gaps = uncovered(p, blockSet);
            int before = restored;
            for (int i = 0; i < altAddrs.size(); i++) {
                Address a = altAddrs.get(i);
                if (fm.getFunctionAt(a) != null) continue;
                if (!gaps.contains(a)) continue;
                if (create(p, a, altNames.get(i))) {
                    restored++;
                    println("  restored alt entry as function: " + a + " " + altNames.get(i));
                } else {
                    failed++;
                }
            }
            if (restored == before) break;
        }
        println("alt entries restored as functions: " + restored + "/" + altAddrs.size());

        AddressSet gaps = uncovered(p, blockSet);
        println("uncovered code bytes: " + gaps.getNumAddresses()
                + " in " + gaps.getNumAddressRanges() + " ranges");
        for (AddressRange r : gaps.getAddressRanges()) {
            println("  gap " + r.getMinAddress() + "-" + r.getMaxAddress() + " len=" + r.getLength());
        }

        int count = 0;
        long sumBody = 0;
        for (Function f : fm.getFunctions(true)) {
            if (!blockSet.contains(f.getEntryPoint())) continue;
            count++;
            sumBody += f.getBody().getNumAddresses();
        }
        println("functions in " + CODE_BLOCK + ": " + count + "  body bytes: " + sumBody
                + "  block bytes: " + code.getSize());

        // Code left in no function is the failure this script exists to
        // prevent: a body that stops short hashes differently and then simply
        // does not match, with nothing to show that anything went wrong. It
        // fails the run rather than being printed and committed.
        if (gaps.getNumAddresses() != 0) {
            println("ERR: " + gaps.getNumAddresses() + " byte(s) of code belong to no function");
            return false;
        }
        return failed == 0;
    }

    private boolean create(Program p, Address a, String name) {
        CreateFunctionCmd cmd = new CreateFunctionCmd(name, a, null, SourceType.IMPORTED);
        if (cmd.applyTo(p, monitor)) return true;
        println("FAIL create " + a + " " + name + ": " + cmd.getStatusMsg());
        return false;
    }

    private AddressSet uncovered(Program p, AddressSet blockSet) {
        AddressSet covered = new AddressSet();
        for (Function f : p.getFunctionManager().getFunctions(true)) {
            covered.add(f.getBody());
        }
        return blockSet.subtract(covered);
    }

    private DomainFile lookup(String path) {
        String[] parts = path.split("/");
        DomainFolder cur = state.getProject().getProjectData().getRootFolder();
        for (int i = 0; i < parts.length - 1; i++) {
            if (parts[i].isEmpty()) continue;
            cur = cur.getFolder(parts[i]);
            if (cur == null) return null;
        }
        return cur.getFile(parts[parts.length - 1]);
    }
}
