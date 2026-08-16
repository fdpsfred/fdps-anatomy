// Builds one .fidb from the FD2-synthesised AIL library modules imported under
// a project folder. Separate from FidPopulate.java because that one is wired to
// the three Watcom CRT versions; this library has exactly one version, the
// AIL 3.02 image carved out of FD2.LE.
//
// The library record's LanguageID is taken from the imported modules rather
// than supplied, because FidServiceLibraryIngest silently skips any program
// whose LanguageID differs from the one passed in - a mismatch shows up only as
// a null result, not an error. The OMF loader lands these modules on
// x86:LE:32:default while FDPS.LE is x86:LE:32:watcom; that does not block the
// query, because FidFile.canProcessLanguage compares language descriptions with
// ProcessorSizeComparator, i.e. on processor and size only. Both languages also
// share x86.sla, so the hashes are computed from the same decoding.
//
// Args:
//   libsFolder    - Ghidra project folder holding the imported .obj programs
//   outDir        - filesystem dir to write ail_fd2.fidb into

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidDB;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.db.LibraryRecord;
import ghidra.feature.fid.service.FidPopulateResult;
import ghidra.feature.fid.service.FidService;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;
import ghidra.framework.model.Project;
import ghidra.program.model.lang.LanguageID;
import ghidra.program.model.listing.Program;

public class FidPopulateAil extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] argv = getScriptArgs();
        if (argv.length < 2) {
            println("ERR: need 2 args: libsFolder outDir");
            return;
        }
        String libsFolder = argv[0];
        String outDir = argv[1];

        Project project = state.getProject();
        DomainFolder libs = navigate(project.getProjectData().getRootFolder(), libsFolder);
        if (libs == null) {
            println("ERR: folder not found: " + libsFolder);
            return;
        }

        List<DomainFile> progs = new ArrayList<>();
        LanguageID langId = null;
        for (DomainFile df : libs.getFiles()) {
            Program p = (Program) df.getDomainObject(this, false, false, monitor);
            LanguageID id;
            int functions;
            try {
                id = p.getLanguageID();
                functions = p.getFunctionManager().getFunctionCount();
            } finally {
                p.release(this);
            }
            println("  program: " + df.getName() + "  lang=" + id + "  functions=" + functions);
            if (langId == null) {
                langId = id;
            } else if (!langId.equals(id)) {
                println("ERR: " + df.getName() + " is " + id + ", first module was " + langId
                        + "; the ingest would skip it silently. Normalise the imports first.");
                return;
            }
            progs.add(df);
        }
        if (progs.isEmpty()) {
            println("ERR: no programs under " + libsFolder);
            return;
        }
        println("library language: " + langId);

        File outDirFile = new File(outDir);
        outDirFile.mkdirs();
        File fidbFile = new File(outDirFile, "ail_fd2.fidb");
        if (fidbFile.exists()) fidbFile.delete();

        FidFileManager mgr = FidFileManager.getInstance();
        FidService service = new FidService();
        mgr.createNewFidDatabase(fidbFile);
        FidFile fidFile = mgr.addUserFidFile(fidbFile);
        FidDB fidDb = fidFile.getFidDB(true);
        try {
            FidPopulateResult result = service.createNewLibraryFromPrograms(
                    fidDb,
                    "ail_fd2",
                    "3.02",
                    "FD2.LE-synthesised",
                    progs,
                    null,
                    langId,
                    new ArrayList<LibraryRecord>(),
                    new ArrayList<String>(),
                    monitor);
            if (result == null) {
                println("ERR: ingest produced no result - every program was skipped");
                return;
            }
            println("populate result:");
            println("  attempted: " + result.getTotalAttempted());
            println("  excluded:  " + result.getTotalExcluded());
            println("  added:     " + result.getTotalAdded());
            println("  unresolved symbols: " + result.getUnresolvedSymbols().size());
        } finally {
            fidDb.saveDatabase("populate", monitor);
            fidDb.close();
        }
        mgr.removeUserFile(fidFile); // detach so later queries do not auto-include it
        println("wrote: " + fidbFile.getAbsolutePath() + " (" + fidbFile.length() + " bytes)");
        println("DONE");
    }

    private DomainFolder navigate(DomainFolder root, String path) {
        if (path.equals("/") || path.isEmpty()) return root;
        String[] parts = path.split("/");
        DomainFolder cur = root;
        for (String p : parts) {
            if (p.isEmpty()) continue;
            cur = cur.getFolder(p);
            if (cur == null) return null;
        }
        return cur;
    }
}
