// Dump the pool_fdps function set as JSON, sorted by entry address.
//
// The authoritative worklist is the pool_fdps tag inside the Ghidra program,
// which travels in the versioned snapshot; this dump is only a convenience
// copy for scripts that would rather read a file than query Ghidra. It is
// regenerated, never edited, and it must not be cited as a source of truth --
// correcting a pool changes the tag, and a stale copy on disk would disagree.
//
// Usage: run_ghidra_script with one argument:
//   <output .json path>
//
// Output: [ { "addr", "name", "size", "callers", "callees" }, ... ]
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;

public class DumpFdpsFunctions extends GhidraScript {

	private static final String TAG = "pool_fdps";

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: DumpFdpsFunctions <output .json path>");
			return;
		}

		List<Function> picked = new ArrayList<>();
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			Function fn = it.next();
			for (FunctionTag t : fn.getTags()) {
				if (TAG.equals(t.getName())) {
					picked.add(fn);
					break;
				}
			}
		}
		Collections.sort(picked, (a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));

		StringBuilder sb = new StringBuilder();
		sb.append("[\n");
		for (int i = 0; i < picked.size(); i++) {
			Function fn = picked.get(i);
			sb.append("  {\"addr\": \"").append(pad(fn.getEntryPoint()));
			sb.append("\", \"name\": ").append(quote(fn.getName()));
			sb.append(", \"size\": ").append(fn.getBody().getNumAddresses());
			sb.append(", \"callers\": ").append(addrList(callers(fn)));
			sb.append(", \"callees\": ").append(addrList(callees(fn)));
			sb.append("}");
			if (i + 1 < picked.size()) {
				sb.append(",");
			}
			sb.append("\n");
		}
		sb.append("]\n");

		File out = new File(argv[0]);
		if (out.getParentFile() != null) {
			out.getParentFile().mkdirs();
		}
		Files.write(out.toPath(), sb.toString().getBytes(StandardCharsets.UTF_8));
		println("wrote " + picked.size() + " " + TAG + " functions to " + out);
	}

	private TreeSet<String> callers(Function fn) {
		TreeSet<String> s = new TreeSet<>();
		for (Function f : fn.getCallingFunctions(monitor)) {
			s.add(pad(f.getEntryPoint()));
		}
		return s;
	}

	private TreeSet<String> callees(Function fn) {
		TreeSet<String> s = new TreeSet<>();
		for (Function f : fn.getCalledFunctions(monitor)) {
			s.add(pad(f.getEntryPoint()));
		}
		return s;
	}

	private String addrList(TreeSet<String> s) {
		StringBuilder sb = new StringBuilder("[");
		boolean first = true;
		for (String a : s) {
			if (!first) {
				sb.append(", ");
			}
			sb.append('"').append(a).append('"');
			first = false;
		}
		return sb.append(']').toString();
	}

	private String pad(Address a) {
		String s = Long.toHexString(a.getOffset());
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private String quote(String s) {
		StringBuilder sb = new StringBuilder("\"");
		for (char c : s.toCharArray()) {
			if (c == '"' || c == '\\') {
				sb.append('\\').append(c);
			}
			else if (c < 0x20) {
				sb.append(String.format("\\u%04x", (int) c));
			}
			else {
				sb.append(c);
			}
		}
		return sb.append('"').toString();
	}
}
