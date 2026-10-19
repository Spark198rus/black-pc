// Phase 3 summary of the analysed default.xbe.
// Usage (headless): analyzeHeadless <projdir> black -process default.xbe -noanalysis \
//     -postScript BlackSummary.java <output dir> -scriptPath <this dir>
// Writes ghidra-summary.txt and ghidra-strings.txt into <output dir>.
//@category BlackPort

import java.io.File;
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class BlackSummary extends GhidraScript {

	// Functions whose callers decide how hard the renderer is: direct GPU command writes.
	private static final String[] WATCH = {
		"D3DDevice_BeginPush", "D3DDevice_EndPush", "D3DDevice_CreateVertexShader",
		"D3DDevice_SetPixelShader", "D3DDevice_DrawVertices", "D3DDevice_DrawIndexedVertices",
		"D3DDevice_DrawVerticesUP", "D3DDevice_Swap", "Direct3D_CreateDevice",
	};

	@Override
	public void run() throws Exception {
		String dir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
		FunctionManager fm = currentProgram.getFunctionManager();

		try (PrintWriter w = new PrintWriter(new File(dir, "ghidra-summary.txt"))) {
			w.println("Program: " + currentProgram.getName() + "  functions: " + fm.getFunctionCount());

			// --- coverage per memory block
			Map<String, int[]> perBlock = new TreeMap<>(); // total, named, bytes
			for (Function f : fm.getFunctions(true)) {
				MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
				String bn = b == null ? "?" : b.getName();
				int[] c = perBlock.computeIfAbsent(bn, k -> new int[3]);
				c[0]++;
				if (!f.getName().startsWith("FUN_")) c[1]++;
				c[2] += (int) f.getBody().getNumAddresses();
			}
			w.println("\nFunctions per section (total / named / body bytes)");
			for (Map.Entry<String, int[]> e : perBlock.entrySet()) {
				int[] c = e.getValue();
				w.printf("  %-10s %6d %6d %9d%n", e.getKey(), c[0], c[1], c[2]);
			}

			// --- callers of watched D3D functions
			w.println("\nCallers of watched D3D functions");
			ReferenceManager rm = currentProgram.getReferenceManager();
			for (String name : WATCH) {
				// Ghidra names carry XbSymbolDatabase suffixes (D3DDevice_BeginPush_4), so match by prefix.
				boolean found = false;
				for (Function f : fm.getFunctions(true)) {
					if (!f.getName().startsWith(name)) continue;
					found = true;
					Address target = f.getEntryPoint();
					Set<String> game = new TreeSet<>();
					Set<String> sdk = new TreeSet<>();
					int refs = 0;
					for (Reference r : rm.getReferencesTo(target)) {
						refs++;
						Function cf = fm.getFunctionContaining(r.getFromAddress());
						String who = cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "<no function>@" + r.getFromAddress();
						MemoryBlock cb = currentProgram.getMemory().getBlock(r.getFromAddress());
						(cb != null && cb.getName().equals(".text") ? game : sdk).add(who);
					}
					w.println("  " + f.getName() + " @ " + target + ": " + refs + " refs; "
						+ game.size() + " game(.text) callers, " + sdk.size() + " SDK-internal callers");
					for (String c : game) w.println("      game: " + c);
					for (String c : sdk) w.println("      sdk:  " + c);
				}
				if (!found) w.println("  " + name + ": (no function with that prefix)");
			}

			// --- every D3D-section function the game's .text actually calls (named or not)
			w.println("\nD3D-section entry points called from game code (.text): call sites");
			List<String> used = new ArrayList<>();
			for (Function f : fm.getFunctions(true)) {
				MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
				if (b == null || !b.getName().equals("D3D")) continue;
				int fromGame = 0;
				for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
					MemoryBlock cb = currentProgram.getMemory().getBlock(r.getFromAddress());
					if (cb != null && cb.getName().equals(".text")) fromGame++;
				}
				if (fromGame > 0)
					used.add(String.format("  %-60s @ %s  game call sites=%d", f.getName(), f.getEntryPoint(), fromGame));
			}
			Collections.sort(used);
			w.println("  (" + used.size() + " distinct functions)");
			for (String u : used) w.println(u);

			// --- all named D3D/DSOUND/XAPI functions with caller counts
			w.println("\nNamed SDK functions: reference counts");
			List<String> lines = new ArrayList<>();
			for (Function f : fm.getFunctions(true)) {
				String n = f.getName();
				if (n.startsWith("FUN_")) continue;
				MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
				String bn = b == null ? "?" : b.getName();
				if (bn.equals(".text")) continue; // game/CRT code, not SDK
				lines.add(String.format("  %-8s %s @ %s  refs=%d", bn, n, f.getEntryPoint(),
					rm.getReferenceCountTo(f.getEntryPoint())));
			}
			Collections.sort(lines);
			for (String l : lines) w.println(l);
		}

		// --- strings
		int n = 0;
		try (PrintWriter w = new PrintWriter(new File(dir, "ghidra-strings.txt"))) {
			for (Data d : currentProgram.getListing().getDefinedData(true)) {
				if (!d.hasStringValue()) continue;
				Object v = d.getValue();
				if (v == null) continue;
				String s = v.toString().replace("\n", "\\n").replace("\r", "\\r");
				if (s.length() < 4) continue;
				w.println(d.getAddress() + "\t" + s);
				n++;
			}
		}
		println("wrote summary and " + n + " strings to " + dir);
	}
}
