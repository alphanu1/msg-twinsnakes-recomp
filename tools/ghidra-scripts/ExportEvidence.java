// Export an auditable inventory of an analysed binary.
//
// Exports FACTS ONLY: addresses, sizes, names, call counts. It deliberately
// does NOT export decompiled pseudo-C, which is a derivative of the game's
// copyrighted code and must never enter this repository (project rule 8).
//
// Run via tools/ghidra-analyse.sh.
//@category TwinSnakes
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ExportEvidence extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "evidence.txt";
        PrintWriter w = new PrintWriter(new FileWriter(out));
        w.println("# Ghidra function inventory — facts only, no decompiled code.");
        w.println("# program: " + currentProgram.getName());
        w.println("# language: " + currentProgram.getLanguageID());
        w.println("# executable sha256: " + currentProgram.getExecutableSHA256());
        w.println("# format: <address> <size> <name> <incoming-calls>");
        w.println();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            int refs = 0;
            ReferenceIterator ri = currentProgram.getReferenceManager()
                    .getReferencesTo(f.getEntryPoint());
            while (ri.hasNext()) { ri.next(); refs++; }
            w.printf("%s %d %s %d%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
                     f.getName(), refs);
            n++;
        }
        w.close();
        println("exported " + n + " functions to " + out);
    }
}
