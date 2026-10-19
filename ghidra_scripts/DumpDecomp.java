// Decompile functions to C text files.
// Args: <output dir> <spec>...   where spec is a hex address ("2bac0") or a range ("2a000-48000").
// Writes <output dir>/<name>.c per function plus <output dir>/_all.c with everything concatenated.
// Output is derived from the game binary: keep it out of version control.
//@category BlackPort

import java.io.File;
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DumpDecomp extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		File dir = new File(args[0]);
		dir.mkdirs();

		FunctionManager fm = currentProgram.getFunctionManager();
		TreeMap<Address, Function> todo = new TreeMap<>();
		for (int i = 1; i < args.length; i++) {
			String spec = args[i];
			if (spec.contains("-")) {
				String[] p = spec.split("-");
				Address lo = toAddr(Long.parseLong(p[0], 16));
				Address hi = toAddr(Long.parseLong(p[1], 16));
				for (Function f : fm.getFunctions(lo, true)) {
					if (f.getEntryPoint().compareTo(hi) > 0) break;
					todo.put(f.getEntryPoint(), f);
				}
			}
			else {
				Address a = toAddr(Long.parseLong(spec, 16));
				Function f = fm.getFunctionAt(a);
				if (f == null) f = fm.getFunctionContaining(a);
				if (f == null) {
					println("no function at " + spec);
					continue;
				}
				todo.put(f.getEntryPoint(), f);
			}
		}

		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		int ok = 0, bad = 0;
		try (PrintWriter all = new PrintWriter(new File(dir, "_all.c"))) {
			for (Function f : todo.values()) {
				DecompileResults r = di.decompileFunction(f, 90, monitor);
				String c;
				if (r.decompileCompleted()) {
					c = r.getDecompiledFunction().getC();
					ok++;
				}
				else {
					c = "// decompile failed: " + r.getErrorMessage() + "\n";
					bad++;
				}
				String header = "// ===== " + f.getName() + " @ " + f.getEntryPoint() + " (" +
					f.getBody().getNumAddresses() + " bytes) =====\n";
				all.print(header + c + "\n");
				try (PrintWriter one = new PrintWriter(new File(dir, f.getName() + ".c"))) {
					one.print(header + c);
				}
			}
		}
		println("decompiled " + ok + " functions (" + bad + " failed) into " + dir);
	}
}
