// Ticket 17's gate: is the global-data state of FDPS.LE self-consistent?
//
// Two kinds of count, and the difference is what lets this run while the ticket
// is half done:
//
//   pending    anchors still wearing the label Ghidra generated for them. Nobody
//              has judged them yet. It counts down as the ticket proceeds and is
//              not a failure.
//   violation  something a landed verdict got wrong: a name that breaks the
//              canon in rebuild_info/naming.md, an address inside a name, two
//              anchors claiming one symbol, a global named but left with an
//              undefined type, a variable straddling the _edata or _end
//              boundary, or a label stranded inside another object. Never
//              acceptable, at any point in the run.
//
// It also reports the DGROUP segment boundaries as measured, because ticket 17
// owes an answer on where initialised data stops and _BSS begins, and a claim
// that is re-measured every round is worth more than one written down once.
//
// Usage: run_ghidra_script with one argument:
//   <work dir>          writes audit.json under it; the counts also go to stdout
//
//@category FDPS
//@runtime Java

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.AbstractStringDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class AuditGlobals extends GhidraScript {

	private static final long DATA_END = 0x63930L;   // _edata
	private static final long BSS_END = 0x6a3bcL;    // _end
	private static final long OBJ2_FILE_END = 0x64000L;

	private Memory mem;
	private MemoryBlock codeBlock;

	private final List<String> violations = new ArrayList<>();
	private final List<String> pendingList = new ArrayList<>();

	@Override
	public void run() throws Exception {
		if (!"FDPS.LE".equals(currentProgram.getName())) {
			throw new IllegalStateException("expected FDPS.LE, got " + currentProgram.getName());
		}
		String[] argv = getScriptArgs();
		if (argv.length < 1) {
			println("ERR: usage: AuditGlobals <work dir>");
			return;
		}
		mem = currentProgram.getMemory();
		codeBlock = mem.getBlock(".object1");

		Map<Address, Set<String>> pools = referencedAddressesByPool();

		int named = 0;
		int pending = 0;
		int stillUndefined = 0;
		Map<String, String> claimed = new HashMap<>();

		Set<Address> anchors = new TreeSet<>(pools.keySet());
		for (String bn : new String[] { ".object2", ".object3" }) {
			MemoryBlock b = mem.getBlock(bn);
			if (b == null) {
				continue;
			}
			for (Data d : currentProgram.getListing().getDefinedData(
					new AddressSet(b.getStart(), b.getEnd()), true)) {
				anchors.add(d.getAddress());
			}
		}

		for (Address a : anchors) {
			Data d = getDataAt(a);
			if (d != null && d.getDataType() instanceof AbstractStringDataType) {
				continue;   // string literals are not named globals; see build_worklist.py
			}
			Symbol s = getSymbolAt(a);
			String key = String.format("%08x", a.getOffset());

			// ANALYSIS counts as unjudged, not as named. The LE loader stamps
			// fix_off32_<addr> on every fixup target it finds -- 6,844 of them,
			// which is why the snapshot exporter drops them too. They are
			// Ghidra's guess at where a relocation points, not anybody's verdict,
			// and a verdict lands as USER_DEFINED.
			if (s == null || s.getSource() == SourceType.DEFAULT
					|| s.getSource() == SourceType.ANALYSIS) {
				// Not judged yet -- unless it now sits inside a named object,
				// in which case its owner speaks for it.
				Data containing = getDataContaining(a);
				boolean owned = containing != null && !containing.getAddress().equals(a);
				if (!owned) {
					pending++;
					if (pendingList.size() < 40) {
						pendingList.add(key);
					}
				}
				continue;
			}

			named++;
			String name = s.getName();
			String pool = poolOf(pools.get(a));

			if (claimed.containsKey(name)) {
				violations.add(key + ": name " + name + " already claimed by " + claimed.get(name));
			}
			else {
				claimed.put(name, key);
			}

			if (!name.startsWith("binary_artifact_") && hasAddressInIt(name)) {
				violations.add(key + ": name " + name + " carries an address");
			}

			String prefixProblem = checkPrefix(name, pool);
			if (prefixProblem != null) {
				violations.add(key + ": " + prefixProblem);
			}

			if (d == null) {
				violations.add(key + ": " + name + " is named but has no data defined at it");
			}
			else {
				String tn = d.getDataType().getName();
				if (tn.startsWith("undefined")) {
					stillUndefined++;
					violations.add(key + ": " + name + " is named but still typed " + tn);
				}
				long start = a.getOffset();
				long end = start + d.getLength();
				if (straddles(start, end, DATA_END)) {
					violations.add(key + ": " + name + " straddles _edata (0x63930)");
				}
				if (straddles(start, end, BSS_END)) {
					violations.add(key + ": " + name + " straddles _end (0x6a3bc)");
				}
			}

			Data containing = getDataContaining(a);
			if (containing != null && !containing.getAddress().equals(a)) {
				violations.add(key + ": " + name + " is a label stranded inside the object at "
					+ String.format("%08x", containing.getAddress().getOffset()));
			}
		}

		// Struct type names answer to the same canon as symbols, and nothing
		// else in the pipeline checks them.
		int structsChecked = checkStructNames();

		// The boundary the ticket owes an answer on, re-measured rather than
		// quoted: the last non-zero initialised byte below the file-backed end
		// of object 2 should fall below _edata.
		long lastNonZero = lastNonZeroBefore(OBJ2_FILE_END);

		JsonObject out = new JsonObject();
		out.addProperty("anchors", anchors.size());
		out.addProperty("named", named);
		out.addProperty("pending", pending);
		out.addProperty("still_undefined", stillUndefined);
		out.addProperty("project_types", structsChecked);
		out.addProperty("violations", violations.size());
		out.addProperty("edata", String.format("%08x", DATA_END));
		out.addProperty("end", String.format("%08x", BSS_END));
		out.addProperty("last_nonzero_initialised_byte", String.format("%08x", lastNonZero));
		out.addProperty("edata_consistent", lastNonZero < DATA_END);
		JsonArray va = new JsonArray();
		for (String v : violations) {
			va.add(v);
		}
		out.add("violation_list", va);
		JsonArray pa = new JsonArray();
		for (String p : pendingList) {
			pa.add(p);
		}
		out.add("pending_sample", pa);

		File dir = new File(argv[0]);
		dir.mkdirs();
		Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
		try (PrintWriter w = new PrintWriter(new OutputStreamWriter(
				new FileOutputStream(new File(dir, "audit.json")), StandardCharsets.UTF_8))) {
			w.print(gson.toJson(out));
		}

		println("anchors = " + anchors.size());
		println("named = " + named);
		println("pending = " + pending);
		println("violations = " + violations.size());
		int shown = 0;
		for (String v : violations) {
			if (shown++ >= 60) {
				println("  ... " + (violations.size() - 60) + " more in audit.json");
				break;
			}
			println("  VIOLATION " + v);
		}
		println("last non-zero initialised byte in object 2 = "
			+ String.format("%08x", lastNonZero)
			+ " (_edata = 00063930, consistent = " + (lastNonZero < DATA_END) + ")");
	}

	private Map<Address, Set<String>> referencedAddressesByPool() {
		Map<Address, Set<String>> out = new TreeMap<>();
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext()) {
			Instruction ins = it.next();
			Function f = getFunctionContaining(ins.getAddress());
			String pool = f == null ? "untagged" : tagOf(f);
			for (Reference r : ins.getReferencesFrom()) {
				Address to = r.getToAddress();
				if (to == null || !inScope(to)) {
					continue;
				}
				RefType t = r.getReferenceType();
				if (t.isCall() || t.isJump() || t.isFlow()) {
					continue;
				}
				out.computeIfAbsent(to, k -> new HashSet<>()).add(pool);
			}
		}
		return out;
	}

	private boolean inScope(Address a) {
		if (a.getAddressSpace().isOverlaySpace()) {
			return false;
		}
		MemoryBlock b = mem.getBlock(a);
		if (b == null) {
			return false;
		}
		if (b.isWrite()) {
			return true;
		}
		return b.equals(codeBlock) && getFunctionContaining(a) == null;
	}

	private String tagOf(Function f) {
		for (FunctionTag t : f.getTags()) {
			if (t.getName().startsWith("pool_")) {
				return t.getName().substring("pool_".length());
			}
		}
		return "untagged";
	}

	/** One pool if every toucher agrees, otherwise "mixed". */
	private String poolOf(Set<String> pools) {
		if (pools == null || pools.isEmpty()) {
			return "unknown";
		}
		if (pools.size() == 1) {
			return pools.iterator().next();
		}
		return "mixed";
	}

	/**
	 * An address hiding in a name is a refusal to decide what the thing is. It
	 * is detected per underscore-delimited word rather than by scanning for a
	 * run of hex characters, and the word has to resolve to somewhere inside
	 * this program before it counts. Both conditions earn their keep: "decade"
	 * and "added" are entirely hex digits, and a rule that only looked at the
	 * characters would reject them.
	 */
	private boolean hasAddressInIt(String name) {
		for (String word : name.split("_")) {
			if (word.length() < 4 || word.length() > 8) {
				continue;
			}
			boolean allHex = true;
			for (int i = 0; i < word.length(); i++) {
				char c = word.charAt(i);
				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
					allHex = false;
					break;
				}
			}
			if (!allHex) {
				continue;
			}
			try {
				if (mem.contains(currentProgram.getAddressFactory()
						.getDefaultAddressSpace().getAddress(Long.parseLong(word, 16)))) {
					return true;
				}
			}
			catch (Exception e) {
				// Not an address this program has; not an address in a name.
			}
		}
		return false;
	}

	/**
	 * The canon is rebuild_info/naming.md. A global reached only by game code
	 * takes data_fdps_; one reached only by the sound library takes data_ail_ or
	 * the AIL_ name it has upstream; CRT data either carries the library's own
	 * symbol -- which no prefix rule can predict -- or says with L$N_ /
	 * crt_equivalent_ that it could not be matched. Anything a linker or
	 * compiler produced is binary_artifact_. Where the pool is unknown or mixed
	 * no prefix can be required, so none is.
	 */
	private String checkPrefix(String name, String pool) {
		// binary_artifact_ cuts across the pools rather than sitting beside
		// them. wcc386 parks the initialiser image of an auto array in the
		// read-only data next to the function that declares it, so the only code
		// that ever reaches those bytes is game code -- and they are still
		// compiler output, not a variable anybody wrote. Requiring data_fdps_
		// there would force a name that claims the original had a global it
		// never had.
		if (name.startsWith("binary_artifact_")) {
			return null;
		}
		// There is no crt_ prefix in this project, in any pool. A CRT symbol
		// carries the library's own name so that wlink resolves it against the
		// real .LIB; where the public name cannot be identified it is L$N_; and
		// crt_equivalent_ means something hand written because it matched no
		// library object at all. crt_tm and crt_file are the failure this
		// catches, and it is a rule the prompts alone did not hold: the first
		// run of this ticket proposed twenty-one such names before anything
		// mechanical objected.
		if (name.startsWith("crt_") && !name.startsWith("crt_equivalent_")) {
			return "name " + name + " uses a bare crt_ prefix; CRT symbols take the "
				+ "library's own name, L$N_, or crt_equivalent_";
		}
		if ("fdps".equals(pool)) {
			return name.startsWith("data_fdps_") ? null
				: "name " + name + " is reached only by pool_fdps code but is not data_fdps_";
		}
		if ("ail".equals(pool)) {
			return name.startsWith("data_ail_") || name.startsWith("AIL_") ? null
				: "name " + name + " is reached only by pool_ail code but is neither data_ail_ nor AIL_";
		}
		if ("crt".equals(pool)) {
			if (name.startsWith("data_fdps_") || name.startsWith("data_ail_")) {
				return "name " + name + " is reached only by pool_crt code but wears another pool's prefix";
			}
			return null;
		}
		return null;
	}

	/**
	 * The structures this ticket created, checked against the same naming canon
	 * the symbols answer to. Only the categories this project writes into are
	 * looked at -- Ghidra ships thousands of types of its own and none of them
	 * are ours to rename.
	 *
	 * @return how many types were examined
	 */
	private int checkStructNames() {
		int seen = 0;
		java.util.Iterator<ghidra.program.model.data.DataType> it =
			currentProgram.getDataTypeManager().getAllDataTypes();
		while (it.hasNext()) {
			ghidra.program.model.data.DataType dt = it.next();
			String path = dt.getCategoryPath().getPath();
			if (!path.startsWith("/fdps")) {
				continue;
			}
			seen++;
			String name = dt.getName();
			if (name.startsWith("crt_") && !name.startsWith("crt_equivalent_")) {
				violations.add("type " + name + " uses a bare crt_ prefix; a library "
					+ "structure takes the library's own name, or L$N_ where it cannot "
					+ "be identified");
			}
			if (name.endsWith("_t")) {
				violations.add("type " + name + " carries a _t suffix, which the canon drops");
			}
			if (hasAddressInIt(name)) {
				violations.add("type " + name + " carries an address");
			}
		}
		return seen;
	}

	private static boolean straddles(long start, long end, long boundary) {
		return start < boundary && end > boundary;
	}

	private long lastNonZeroBefore(long limit) {
		MemoryBlock b = mem.getBlock(".object2");
		if (b == null) {
			return 0;
		}
		long start = b.getStart().getOffset();
		int len = (int) (limit - start);
		byte[] buf = new byte[len];
		try {
			mem.getBytes(b.getStart(), buf);
		}
		catch (Exception e) {
			return -1;
		}
		for (int i = len - 1; i >= 0; i--) {
			if (buf[i] != 0) {
				return start + i;
			}
		}
		return start;
	}
}
