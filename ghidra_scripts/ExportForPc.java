// Export the analysed program so another machine can open it without re-running analysis.
// Args: <ghidra dir> <notes dir>
// Writes <ghidra dir>/default.xbe.gzf (open on any OS: Ghidra File > Import File) and
// <notes dir>/functions.csv (address, name, size, section, ref count; no code, no bytes).
//@category BlackPort

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.MemoryBlock;

public class ExportForPc extends GhidraScript {

	@Override
	public void run() throws Exception {
		File ghidraDir = new File(getScriptArgs()[0]);
		File notesDir = new File(getScriptArgs()[1]);
		ghidraDir.mkdirs();

		File gzf = new File(ghidraDir, "default.xbe.gzf");
		if (gzf.exists()) gzf.delete();
		currentProgram.getDomainFile().packFile(gzf, monitor);
		println("packed " + gzf + " (" + gzf.length() + " bytes)");

		FunctionManager fm = currentProgram.getFunctionManager();
		int n = 0;
		try (PrintWriter w = new PrintWriter(new File(notesDir, "functions.csv"))) {
			w.println("address,name,size,section,refs");
			for (Function f : fm.getFunctions(true)) {
				MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
				w.printf("%s,%s,%d,%s,%d%n", f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses(),
					b == null ? "?" : b.getName(),
					currentProgram.getReferenceManager().getReferenceCountTo(f.getEntryPoint()));
				n++;
			}
		}
		println("wrote functions.csv with " + n + " functions");
	}
}
