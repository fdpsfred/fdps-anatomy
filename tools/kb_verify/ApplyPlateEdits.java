// Transcribe settled plate-comment changes into FDPS.LE, exactly as written.
//
// Input: one TSV file (the script's first argument), one change per line:
//   <address> TAB <mode> TAB <base64 UTF-8 old> TAB <base64 UTF-8 new>
// Modes:
//   replace  old must occur exactly once in the plate; it is replaced by new
//   append   new is appended to the plate after a blank line (old is empty)
//   full     the plate becomes new; old must equal the current plate, so a
//            plate that changed since the verdict was written is refused
// Nothing is judged here: a change whose precondition fails is reported and
// skipped, never adapted.  The plate is the PLATE comment at the address, so
// a data global's plate is written the same way as a function's.
//
// @category FDPS

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Base64;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Listing;

public class ApplyPlateEdits extends GhidraScript {

    private static String decode(String b64) {
        return new String(Base64.getDecoder().decode(b64), StandardCharsets.UTF_8);
    }

    private static int count(String hay, String needle) {
        int n = 0;
        for (int i = hay.indexOf(needle); i >= 0; i = hay.indexOf(needle, i + needle.length())) {
            n++;
        }
        return n;
    }

    @Override
    protected void run() throws Exception {
        if (!currentProgram.getName().equals("FDPS.LE")) {
            throw new IllegalStateException("current program is " + currentProgram.getName());
        }
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ApplyPlateEdits <edits.tsv>");
        }
        List<String> lines = Files.readAllLines(new File(args[0]).toPath(), StandardCharsets.UTF_8);
        Listing listing = currentProgram.getListing();
        int applied = 0, refused = 0;
        for (String line : lines) {
            if (line.isBlank()) {
                continue;
            }
            String[] f = line.split("\t", -1);
            Address addr = toAddr(f[0]);
            String mode = f[1];
            String oldText = decode(f[2]);
            String newText = decode(f[3]);
            String plate = listing.getComment(CodeUnit.PLATE_COMMENT, addr);
            String current = plate == null ? "" : plate;
            String result;
            if (mode.equals("replace")) {
                int c = count(current, oldText);
                if (oldText.isEmpty() || c != 1) {
                    println("REFUSED " + f[0] + " replace: old occurs " + c + " times");
                    refused++;
                    continue;
                }
                result = current.replace(oldText, newText);
            } else if (mode.equals("append")) {
                result = current.isEmpty() ? newText : current + "\n\n" + newText;
            } else if (mode.equals("full")) {
                if (!current.equals(oldText)) {
                    println("REFUSED " + f[0] + " full: the plate is not the one the verdict saw");
                    refused++;
                    continue;
                }
                result = newText;
            } else {
                println("REFUSED " + f[0] + " unknown mode " + mode);
                refused++;
                continue;
            }
            listing.setComment(addr, CodeUnit.PLATE_COMMENT, result);
            println("APPLIED " + f[0] + " " + mode);
            applied++;
        }
        println("applied=" + applied + " refused=" + refused);
    }
}
