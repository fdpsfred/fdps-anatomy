// Export the analysis state of the current program as sorted, deterministic UTF-8 text files.
// Usage (Ghidra script arguments): [output directory]
//   Defaults to <repo>/ghidra_snapshot when no argument is given.
//@category FDPS

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.TreeMap;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.BuiltInDataType;
import ghidra.program.model.data.Composite;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinition;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

public class ExportGhidraSnapshot extends GhidraScript {

	private static final String DEFAULT_OUTPUT_DIR =
		"C:/Users/fdpsf/Documents/fdps-anatomy/ghidra_snapshot";

	// Artifacts the LE loader generates from the relocation table, one per fixup site.
	// They are import output rather than analysis results, so they are counted but not listed.
	private static final Pattern FIXUP_LABEL = Pattern.compile("fix_off32_[0-9a-f]{8}");
	private static final Pattern FIXUP_COMMENT = Pattern.compile("fixup to -> [0-9a-f]{8}");
	private static final String LOADER_CATEGORY_PREFIX = "/_le";

	private final TreeMap<String, Long> counts = new TreeMap<>();

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		Path outDir = Paths.get(args.length > 0 && !args[0].isBlank() ? args[0] : DEFAULT_OUTPUT_DIR);
		Files.createDirectories(outDir);

		write(outDir.resolve("functions.txt"), functions());
		write(outDir.resolve("comments.txt"), comments());
		write(outDir.resolve("data_types.txt"), dataTypes());
		write(outDir.resolve("labels.txt"), labels());
		write(outDir.resolve("data.txt"), data());
		write(outDir.resolve("bookmarks.txt"), bookmarks());
		// Written last: its counters are filled in by the sections above.
		write(outDir.resolve("program.txt"), program());

		println("Snapshot written to " + outDir.toAbsolutePath());
		for (java.util.Map.Entry<String, Long> e : counts.entrySet()) {
			println("  " + e.getKey() + " = " + e.getValue());
		}
	}

	// ---------------------------------------------------------------- sections

	private List<String> program() {
		List<String> out = new ArrayList<>();
		out.add("# Program identity and section counters. No timestamps: identical state exports identically.");
		out.add("name = " + currentProgram.getName());
		out.add("format = " + currentProgram.getExecutableFormat());
		out.add("language = " + currentProgram.getLanguageID());
		out.add("compilerSpec = " + currentProgram.getCompilerSpec().getCompilerSpecID());
		out.add("imageBase = " + currentProgram.getImageBase());
		out.add("executableMD5 = " + currentProgram.getExecutableMD5());
		out.add("executableSHA256 = " + currentProgram.getExecutableSHA256());
		out.add("");
		out.add("# name | range | permissions | initialized | byte size");
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			out.add(String.format("block %s | %s-%s | %s%s%s | init=%s | 0x%x",
				b.getName(), b.getStart(), b.getEnd(),
				b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-",
				b.isInitialized(), b.getSize()));
		}
		out.add("");
		for (java.util.Map.Entry<String, Long> e : counts.entrySet()) {
			out.add(e.getKey() + " = " + e.getValue());
		}
		return out;
	}

	private List<String> functions() {
		List<String> out = new ArrayList<>();
		out.add("# address | body size | calling convention | signature source | stack purge | flags | tags | prototype");
		out.add("# An indented 'var' line follows a function for each explicitly named local variable.");
		long total = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			total++;
			List<String> flags = new ArrayList<>();
			if (f.isThunk()) {
				flags.add("thunk->" + f.getThunkedFunction(true).getEntryPoint());
			}
			if (f.hasNoReturn()) {
				flags.add("noreturn");
			}
			if (f.isInline()) {
				flags.add("inline");
			}
			if (f.isExternal()) {
				flags.add("external");
			}
			if (f.hasVarArgs()) {
				flags.add("varargs");
			}
			if (f.hasCustomVariableStorage()) {
				flags.add("customstorage");
			}
			List<String> tags = new ArrayList<>();
			f.getTags().forEach(t -> tags.add(t.getName()));
			Collections.sort(tags);

			out.add(String.format("%s | 0x%x | %s | %s | 0x%x | %s | %s | %s",
				f.getEntryPoint(),
				f.getBody().getNumAddresses(),
				f.getCallingConventionName(),
				f.getSignatureSource(),
				f.getStackPurgeSize(),
				flags.isEmpty() ? "-" : String.join(",", flags),
				tags.isEmpty() ? "-" : String.join(",", tags),
				f.getSignature().getPrototypeString(true)));

			if (f.hasCustomVariableStorage()) {
				List<String> storage = new ArrayList<>();
				storage.add("return=" + f.getReturn().getVariableStorage());
				for (Parameter p : f.getParameters()) {
					storage.add(p.getName() + "=" + p.getVariableStorage());
				}
				out.add("    storage " + String.join(" ", storage));
			}
			List<Variable> locals = new ArrayList<>();
			for (Variable v : f.getLocalVariables()) {
				if (v.getSource() != SourceType.DEFAULT) {
					locals.add(v);
				}
			}
			locals.sort(Comparator.comparing(v -> v.getVariableStorage().toString()));
			for (Variable v : locals) {
				out.add(String.format("    var %s | %s | %s",
					v.getVariableStorage(), v.getDataType().getName(), v.getName()));
			}
		}
		counts.put("counts.functions", total);
		return out;
	}

	private List<String> comments() {
		List<String> out = new ArrayList<>();
		out.add("# One block per comment, opened by '=== <address> [<type>]'. Blocks are address-ordered,");
		out.add("# then ordered by type within an address. Loader fixup comments are excluded.");
		CommentType[] types = CommentType.values();
		long kept = 0;
		long skipped = 0;
		AddressIterator it = currentProgram.getListing().getCommentAddressIterator(
			currentProgram.getMemory(), true);
		while (it.hasNext()) {
			Address a = it.next();
			for (CommentType t : types) {
				String c = currentProgram.getListing().getComment(t, a);
				if (c == null) {
					continue;
				}
				if (t == CommentType.PRE && FIXUP_COMMENT.matcher(c).matches()) {
					skipped++;
					continue;
				}
				kept++;
				out.add("=== " + a + " [" + t + "]");
				for (String line : normalize(c).split("\n", -1)) {
					out.add(line);
				}
				out.add("");
			}
		}
		counts.put("counts.comments", kept);
		counts.put("counts.excluded.loaderFixupComments", skipped);
		return out;
	}

	private List<String> dataTypes() {
		List<String> out = new ArrayList<>();
		out.add("# Composites, enums, typedefs and function definitions owned by the program.");
		out.add("# Built-in types and types derived from them (pointers, arrays) carry no analysis");
		out.add("# decision and are omitted, as are the loader's " + LOADER_CATEGORY_PREFIX + " fixup types.");
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		List<DataType> kept = new ArrayList<>();
		long skipped = 0;
		Iterator<DataType> it = dtm.getAllDataTypes();
		while (it.hasNext()) {
			DataType d = it.next();
			if (d.getCategoryPath().getPath().startsWith(LOADER_CATEGORY_PREFIX)) {
				skipped++;
				continue;
			}
			if (d instanceof BuiltInDataType || d instanceof Pointer || d instanceof Array) {
				continue;
			}
			if (d instanceof Composite || d instanceof ghidra.program.model.data.Enum
					|| d instanceof TypeDef || d instanceof FunctionDefinition) {
				kept.add(d);
			}
		}
		kept.sort(Comparator.comparing(d -> d.getPathName()));
		for (DataType d : kept) {
			if (d instanceof Composite) {
				Composite c = (Composite) d;
				out.add(String.format("%s %s | size=0x%x | align=0x%x | packing=%s",
					(c instanceof Structure) ? "struct" : "union",
					c.getPathName(), c.getLength(), c.getAlignment(),
					c.isPackingEnabled() ? "enabled" : "disabled"));
				for (DataTypeComponent m : c.getDefinedComponents()) {
					out.add(String.format("    +0x%03x | 0x%-4x | %s | %s%s",
						m.getOffset(), m.getLength(), m.getDataType().getPathName(),
						m.getFieldName() == null ? "-" : m.getFieldName(),
						m.getComment() == null ? "" : " | " + normalize(m.getComment()).replace("\n", " ")));
				}
			}
			else if (d instanceof ghidra.program.model.data.Enum) {
				ghidra.program.model.data.Enum e = (ghidra.program.model.data.Enum) d;
				out.add(String.format("enum %s | size=0x%x", e.getPathName(), e.getLength()));
				List<String> names = new ArrayList<>(java.util.Arrays.asList(e.getNames()));
				Collections.sort(names);
				for (String n : names) {
					out.add(String.format("    %s = 0x%x", n, e.getValue(n)));
				}
			}
			else if (d instanceof TypeDef) {
				out.add("typedef " + d.getPathName() + " -> "
					+ ((TypeDef) d).getDataType().getPathName());
			}
			else {
				FunctionDefinition fd = (FunctionDefinition) d;
				List<String> params = new ArrayList<>();
				for (ParameterDefinition p : fd.getArguments()) {
					params.add(p.getDataType().getPathName() + " " + p.getName());
				}
				if (fd.hasVarArgs()) {
					params.add("...");
				}
				out.add("funcdef " + d.getPathName() + " = " + fd.getReturnType().getPathName()
					+ "(" + String.join(", ", params) + ")");
			}
			String note = d.getDescription();
			if (note != null && !note.isBlank()) {
				out.add("    # " + normalize(note).replace("\n", " "));
			}
		}
		counts.put("counts.dataTypes", (long) kept.size());
		counts.put("counts.excluded.loaderFixupTypes", skipped);
		return out;
	}

	private List<String> labels() {
		List<String> out = new ArrayList<>();
		out.add("# address | symbol type | source | primary | namespace::name");
		out.add("# Function symbols live in functions.txt; automatic (DAT_/LAB_) and loader fixup");
		out.add("# labels are excluded because they carry no naming decision.");
		List<Symbol> kept = new ArrayList<>();
		long skipped = 0;
		for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
			if (s.getSource() == SourceType.DEFAULT || s.getSymbolType() == SymbolType.FUNCTION) {
				continue;
			}
			if (FIXUP_LABEL.matcher(s.getName()).matches()) {
				skipped++;
				continue;
			}
			kept.add(s);
		}
		kept.sort(Comparator.comparing((Symbol s) -> s.getAddress().toString())
			.thenComparing(s -> s.getName(true)));
		for (Symbol s : kept) {
			out.add(String.format("%s | %s | %s | %s | %s",
				s.getAddress(), s.getSymbolType(), s.getSource(),
				s.isPrimary() ? "primary" : "-", s.getName(true)));
		}
		counts.put("counts.labels", (long) kept.size());
		counts.put("counts.excluded.loaderFixupLabels", skipped);
		return out;
	}

	private List<String> data() {
		List<String> out = new ArrayList<>();
		out.add("# address | byte size | data type | primary symbol name | symbol source");
		out.add("# Every defined data item, so that both global naming and type application are covered.");
		long total = 0;
		Listing listing = currentProgram.getListing();
		DataIterator it = listing.getDefinedData(true);
		while (it.hasNext()) {
			Data d = it.next();
			total++;
			Symbol s = d.getPrimarySymbol();
			out.add(String.format("%s | 0x%x | %s | %s | %s",
				d.getAddress(), d.getLength(), d.getDataType().getPathName(),
				s == null ? "-" : s.getName(true),
				s == null ? "-" : s.getSource().toString()));
		}
		counts.put("counts.definedData", total);
		return out;
	}

	private List<String> bookmarks() {
		List<String> out = new ArrayList<>();
		out.add("# address | type | category | comment");
		out.add("# Error bookmarks mark disassembly damage, so repair progress shows up as a diff.");
		List<String> rows = new ArrayList<>();
		Iterator<Bookmark> it = currentProgram.getBookmarkManager().getBookmarksIterator();
		while (it.hasNext()) {
			Bookmark b = it.next();
			rows.add(String.format("%s | %s | %s | %s",
				b.getAddress(), b.getTypeString(), b.getCategory(),
				normalize(b.getComment()).replace("\n", " ")));
		}
		Collections.sort(rows);
		out.addAll(rows);
		counts.put("counts.bookmarks", (long) rows.size());
		return out;
	}

	// ----------------------------------------------------------------- helpers

	/** Collapses CRLF and CR to LF so the text is independent of how a comment was entered. */
	private static String normalize(String text) {
		return text.replace("\r\n", "\n").replace("\r", "\n");
	}

	private static void write(Path file, List<String> lines) throws IOException {
		StringBuilder sb = new StringBuilder();
		for (String line : lines) {
			sb.append(line).append('\n');
		}
		Files.write(file, sb.toString().getBytes(StandardCharsets.UTF_8));
	}
}
