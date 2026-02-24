# GameHackRE — Reverse Engineering Walkthrough

## What This Is
A guided Ghidra reverse engineering walkthrough that analyzes our own compiled
`Injector.dll` (the GameHack DLL). Since we have the source code, we can directly
compare decompiler output against the original C++ to learn RE fundamentals.

## Setup
- **Ghidra 12.0.3** installed at `C:\ghidra\`
- **Java JDK 21** (Eclipse Temurin) — required by Ghidra
- **Target binary:** `../GameHack/Release/Injector.dll` (22KB, PE32, x86, MSVC)
- **PDB available:** `../GameHack/Release/Injector.pdb` — Ghidra auto-loads this,
  giving full function names and type info

## Files
| File | Purpose |
|---|---|
| `RE_WALKTHROUGH.md` | 14-part guided walkthrough with source code comparisons |
| `launch_ghidra.bat` | Convenience launcher that sets JAVA_HOME and opens Ghidra |
| `GameHackRE.gpr` | Ghidra project file (gitignored — regenerate via headless import) |
| `GameHackRE.rep/` | Ghidra project database (gitignored — large binary files) |

## Regenerating the Ghidra Project
If cloning fresh (no .gpr/.rep files), regenerate with headless analysis:
```batch
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.10.7-hotspot
C:\ghidra\support\analyzeHeadless.bat ^
  "C:\Users\JJ\Documents\hacking\GameHackRE" GameHackRE ^
  -import "C:\Users\JJ\Documents\hacking\GameHack\Release\Injector.dll"
```
This imports the DLL and runs all default analyzers. Takes ~20 seconds.

## Walkthrough Structure (RE_WALKTHROUGH.md)
1. **Part 0** — Launch Ghidra, open pre-analyzed project
2. **Part 1** — Find DllMain (entry point)
3. **Part 2** — Find HackThread (follow CreateThread)
4. **Part 3** — String search technique (the #1 RE method)
5. **Part 4** — Read InstallHook (hook installation, 0xE9 JMP)
6. **Part 5** — Read hkWglSwapBuffers (per-frame hook)
7. **Part 6** — Read WorldToScreen (math: sinf/cosf/tanf)
8. **Part 7** — Read RunAimbot (entity loop + angle calc)
9. **Part 8** — Read DrawESP (OpenGL rendering loop)
10. **Part 9** — SafeRead/SafeWrite (SEH in decompiler)
11. **Part 10** — Global offsets (0x17E0A8, 0x191FCC, etc.)
12. **Part 11** — What's lost vs preserved in compilation
13. **Part 12** — Practice exercises
14. **Part 13** — Ghidra keyboard shortcuts
15. **Part 14** — Challenge: strip PDB and RE without symbols

## Key RE Concepts Covered
- **Strings survive compilation** — #1 starting point for finding functions
- **Constants/offsets survive** — reveal what memory addresses are accessed
- **Import tables** — show what APIs the binary uses (VirtualProtect = hook, glBegin = rendering)
- **Variable names are gone** — become local_XX, param_X, DAT_XXXXXXXX
- **Template instantiations** — each SafeRead<T> becomes a separate FUN_XXXXXXXX
- **SEH** — __try/__except shows as special control flow with _except_handler3
