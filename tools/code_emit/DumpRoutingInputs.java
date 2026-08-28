// Export the whole-program facts that ticket 21.5's routing plan is decided from.
//
// Routing assigns every pool_fdps function and every game-owned global to a
// target .c/.h. That decision is made from the program as a whole -- what a
// function is called by, what it calls, which globals it touches -- so unlike
// the per-function tickets there is no packet to hand out; one dump of the
// whole picture is the input, and the grouping is decided against it.
//
// Line counts come from the decompiler because that is what the estimate is
// meant to approximate: how much C a function will turn into. Body size in
// bytes is also recorded, but bytes and lines diverge badly on table-driven
// code, so the estimate uses lines.
//
// Nothing here is a judgement and nothing is written back to the program.
//
// Usage: run_ghidra_script with one argument:
//   <output dir>        writes functions.json and globals.json under it
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class DumpRoutingInputs extends GhidraScript {

	/** How much of a plate comment's opening sentence is kept as the summary. */
	private static final int MAX_PLATE_HEAD = 300;
	/** Longest string literal reproduced. */
	private static final int MAX_STRING = 120;
	/** How many distinct string literals per function are worth keeping. */
	private static final int MAX_STRINGS_PER_FN = 12;

	private Memory mem;
	private DecompInterface decomp;
	private Function[] byEntry;

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: DumpRoutingInputs <output dir>");
			return;
		}
		File out = new File(argv[0]);
		out.mkdirs();

		mem = currentProgram.getMemory();

		List<Function> all = new ArrayList<>();
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			all.add(it.next());
		}
		byEntry = all.toArray(new Function[0]);
		Arrays.sort(byEntry, (a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));

		decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		JsonArray functions = new JsonArray();
		int dumped = 0;
		for (Function f : byEntry) {
			if (!"fdps".equals(poolOf(f))) {
				continue;
			}
			functions.add(functionOf(f));
			dumped++;
			if (dumped % 50 == 0) {
				println("functions dumped " + dumped);
			}
		}
		decomp.dispose();

		JsonObject froot = new JsonObject();
		froot.addProperty("program", currentProgram.getName());
		froot.addProperty("function_count", byEntry.length);
		froot.addProperty("fdps_count", dumped);
		froot.add("functions", functions);
		writeJson(new File(out, "functions.json"), froot);

		JsonArray globals = dumpGlobals();
		JsonObject groot = new JsonObject();
		groot.addProperty("program", currentProgram.getName());
		groot.addProperty("count", globals.size());
		groot.add("globals", globals);
		writeJson(new File(out, "globals.json"), groot);

		println("pool_fdps functions = " + dumped);
		println("game-owned globals = " + globals.size());
		println("output = " + out.getAbsolutePath());
	}

	// --------------------------------------------------------- functions

	private JsonObject functionOf(Function f) throws Exception {
		JsonObject o = new JsonObject();
		AddressSetView body = f.getBody();
		o.addProperty("addr", key(f));
		o.addProperty("name", f.getName());
		o.addProperty("size", body.getNumAddresses());
		o.addProperty("cc", String.valueOf(f.getCallingConventionName()));
		o.addProperty("prototype", f.getSignature().getPrototypeString(true));

		o.add("tags", tagArray(f));

		String plate = getPlateComment(f.getEntryPoint());
		o.addProperty("plate_head", plateHead(plate));
		o.addProperty("has_plate", plate != null && !plate.isEmpty());

		String c = decompile(f);
		o.addProperty("dec_lines", countLines(c));

		JsonArray callers = new JsonArray();
		for (Function n : sorted(f.getCallingFunctions(monitor))) {
			callers.add(neighbour(n));
		}
		o.add("callers", callers);

		JsonArray callees = new JsonArray();
		for (Function n : sorted(f.getCalledFunctions(monitor))) {
			callees.add(neighbour(n));
		}
		o.add("callees", callees);

		// Entry references that are not calls: dispatch-table slots and tail
		// jumps. A function reached only through a table has no caller to group
		// it with, so the table it sits in is what decides its file.
		JsonArray tableRefs = new JsonArray();
		ReferenceIterator ri = currentProgram.getReferenceManager()
			.getReferencesTo(f.getEntryPoint());
		while (ri.hasNext()) {
			Reference r = ri.next();
			if (r.getReferenceType().isCall()) {
				continue;
			}
			JsonObject rj = new JsonObject();
			rj.addProperty("from", hex8(r.getFromAddress().getOffset()));
			rj.addProperty("type", r.getReferenceType().getName());
			Function inside = getFunctionContaining(r.getFromAddress());
			rj.addProperty("in_fn", inside == null ? "" : inside.getName());
			Symbol s = getSymbolAt(r.getFromAddress());
			rj.addProperty("from_symbol", s == null ? "" : s.getName());
			tableRefs.add(rj);
		}
		o.add("non_call_refs", tableRefs);

		collectDataRefs(f, o);
		return o;
	}

	private JsonArray tagArray(Function f) {
		JsonArray a = new JsonArray();
		for (String t : nameSet(f)) {
			a.add(t);
		}
		return a;
	}

	private TreeSet<String> nameSet(Function f) {
		TreeSet<String> s = new TreeSet<>();
		for (FunctionTag t : f.getTags()) {
			s.add(t.getName());
		}
		return s;
	}

	/**
	 * Named data touched by the body, plus the string literals. Which globals a
	 * function reads is how a global finds its owning file: a global read from
	 * one file's functions belongs in that file.
	 */
	private void collectDataRefs(Function f, JsonObject o) {
		TreeSet<String> named = new TreeSet<>();
		TreeSet<String> strings = new TreeSet<>();
		InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
		while (ii.hasNext()) {
			Instruction insn = ii.next();
			for (Reference r : insn.getReferencesFrom()) {
				RefType t = r.getReferenceType();
				if (t.isCall() || t.isFlow()) {
					continue;
				}
				Address to = r.getToAddress();
				if (to == null || !mem.contains(to)) {
					continue;
				}
				Symbol s = getSymbolAt(to);
				if (s != null && s.getSource() != SourceType.DEFAULT) {
					named.add(s.getName());
				}
				String str = stringAt(to);
				if (!str.isEmpty() && strings.size() < MAX_STRINGS_PER_FN) {
					strings.add(str);
				}
			}
		}
		JsonArray na = new JsonArray();
		for (String s : named) {
			na.add(s);
		}
		o.add("named_data", na);
		JsonArray sa = new JsonArray();
		for (String s : strings) {
			sa.add(s);
		}
		o.add("strings", sa);
	}

	private JsonObject neighbour(Function f) {
		JsonObject o = new JsonObject();
		o.addProperty("addr", key(f));
		o.addProperty("name", f.getName());
		o.addProperty("pool", poolOf(f));
		return o;
	}

	// ----------------------------------------------------------- globals

	/**
	 * Every explicitly named data symbol, with the functions that reference it.
	 * All pools are dumped, not just data_fdps_: routing has to be able to see
	 * that a symbol it was about to place is in fact library-owned.
	 */
	private JsonArray dumpGlobals() throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		Map<String, JsonObject> byAddr = new TreeMap<>();
		SymbolIterator si = st.getAllSymbols(false);
		while (si.hasNext()) {
			Symbol s = si.next();
			if (s.getSource() == SourceType.DEFAULT) {
				continue;
			}
			Address a = s.getAddress();
			if (a == null || !mem.contains(a)) {
				continue;
			}
			if (getFunctionAt(a) != null) {
				continue;
			}
			Data d = getDataAt(a);
			JsonObject o = new JsonObject();
			o.addProperty("addr", hex8(a.getOffset()));
			o.addProperty("symbol", s.getName());
			o.addProperty("type", d == null ? "" : d.getDataType().getPathName());
			o.addProperty("size", d == null ? 0 : d.getLength());
			o.addProperty("string", stringAt(a));

			TreeMap<String, String> refs = new TreeMap<>();
			ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
			int total = 0;
			while (ri.hasNext()) {
				Reference r = ri.next();
				total++;
				Function inside = getFunctionContaining(r.getFromAddress());
				if (inside == null) {
					refs.put(hex8(r.getFromAddress().getOffset()), "<no function>");
				}
				else {
					refs.put(key(inside), inside.getName() + " [" + poolOf(inside) + "]");
				}
			}
			o.addProperty("ref_count", total);
			JsonArray ra = new JsonArray();
			for (Map.Entry<String, String> e : refs.entrySet()) {
				ra.add(e.getValue() + " @" + e.getKey());
			}
			o.add("referenced_by", ra);
			byAddr.put(hex8(a.getOffset()) + "/" + s.getName(), o);
		}
		JsonArray out = new JsonArray();
		for (JsonObject o : byAddr.values()) {
			out.add(o);
		}
		return out;
	}

	// ------------------------------------------------------------- utils

	private String decompile(Function f) {
		try {
			DecompileResults res = decomp.decompileFunction(f, 90, monitor);
			if (res == null || !res.decompileCompleted() || res.getDecompiledFunction() == null) {
				return "";
			}
			return res.getDecompiledFunction().getC();
		}
		catch (Exception e) {
			return "";
		}
	}

	private static int countLines(String c) {
		if (c == null || c.isEmpty()) {
			return 0;
		}
		int n = 0;
		for (int i = 0; i < c.length(); i++) {
			if (c.charAt(i) == '\n') {
				n++;
			}
		}
		return c.endsWith("\n") ? n : n + 1;
	}

	private static String plateHead(String plate) {
		if (plate == null || plate.isEmpty()) {
			return "";
		}
		int nl = plate.indexOf('\n');
		String head = (nl < 0 ? plate : plate.substring(0, nl)).trim();
		return head.length() > MAX_PLATE_HEAD ? head.substring(0, MAX_PLATE_HEAD) + "..." : head;
	}

	private String stringAt(Address a) {
		Data d = getDataAt(a);
		if (d != null) {
			StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
			if (sdi != null && sdi != StringDataInstance.NULL_INSTANCE) {
				String v = sdi.getStringValue();
				if (v != null && v.length() >= 3) {
					return trimString(v);
				}
			}
		}
		try {
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < MAX_STRING; i++) {
				int c = mem.getByte(a.add(i)) & 0xff;
				if (c == 0) {
					break;
				}
				if (c < 0x20 || c > 0x7e) {
					return "";
				}
				sb.append((char) c);
			}
			return sb.length() >= 4 ? trimString(sb.toString()) : "";
		}
		catch (Exception e) {
			return "";
		}
	}

	private static String trimString(String s) {
		String out = s.replace("\n", "\\n").replace("\r", "\\r").replace("\"", "'");
		return out.length() > MAX_STRING ? out.substring(0, MAX_STRING) + "..." : out;
	}

	private String poolOf(Function f) {
		for (FunctionTag t : f.getTags()) {
			if (t.getName().startsWith("pool_")) {
				return t.getName().substring(5);
			}
		}
		return "";
	}

	private List<Function> sorted(java.util.Set<Function> set) {
		List<Function> out = new ArrayList<>(set);
		out.sort((a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));
		return out;
	}

	private static String key(Function f) {
		return hex8(f.getEntryPoint().getOffset());
	}

	private static String hex8(long v) {
		String s = Long.toHexString(v);
		while (s.length() < 8) {
			s = "0" + s;
		}
		return s;
	}

	private void writeJson(File f, Object value) throws Exception {
		Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
		try (PrintWriter pw = new PrintWriter(new OutputStreamWriter(
			new FileOutputStream(f), StandardCharsets.UTF_8))) {
			pw.print(gson.toJson(value));
		}
	}
}
