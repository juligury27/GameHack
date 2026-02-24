# GameHack — Game Security Engineering Lab

## Project Overview
Educational game security project targeting **AssaultCube 1.3.0.2** (open-source FPS, no anti-cheat).
The goal is to learn offensive game hacking techniques as a foundation for understanding
anti-cheat systems and game security defense.

## Repository Structure
```
hacking/
├── Injector/          — DLL injector (standalone EXE)
├── GameHack/          — ESP + Aimbot DLL (injected into ac_client.exe)
├── GameHackRE/        — Ghidra reverse engineering walkthrough
├── CLAUDE.md          — This file (root project context)
└── README.md          — GitHub README
```

## Tech Stack
- **Language:** C++ (MSVC, Visual Studio)
- **Target:** AssaultCube 1.3.0.2 (32-bit, x86, OpenGL renderer)
- **Build:** Visual Studio `.vcxproj` / `.slnx` files, Release x86 config
- **RE Tool:** Ghidra 12.0.3 (installed at `C:\ghidra\`)
- **Java:** Eclipse Temurin JDK 21 (for Ghidra)

## Key Offsets (AC 1.3.0.2)
These are the confirmed memory offsets for the game's player struct:

| Offset | Field | Type |
|---|---|---|
| `base + 0x17E0A8` | Local player pointer | `uintptr_t*` |
| `base + 0x191FCC` | Entity list pointer | `uintptr_t*` |
| `player + 0x04/08/0C` | Head X/Y/Z | `float` |
| `player + 0x28/2C/30` | Foot X/Y/Z | `float` |
| `player + 0x34` | Yaw (horizontal aim) | `float` (degrees) |
| `player + 0x38` | Pitch (vertical aim) | `float` (degrees) |
| `player + 0xEC` | Health | `int` |
| `player + 0x205` | Name | `char[16]` |
| `player + 0x30C` | Team | `BYTE` (0=CLA, 1=RVSF) |

## Coding Conventions
- Heavy inline comments explaining WHY, not just what
- Educational style — every technique is documented for a learner audience
- Win32 API usage (no external libraries except OpenGL headers)
- SEH (`__try/__except`) for safe memory reads from game process
- Template functions for type-safe memory read/write (`SafeRead<T>`, `SafeWrite<T>`)

## Build Instructions
1. Open `GameHack/Injector.slnx` or `Injector/GameHack.slnx` in Visual Studio
2. Set configuration to **Release | x86**
3. Build (Ctrl+B)
4. Output goes to `Release/` subfolder

## Usage
```bash
# 1. Start AssaultCube 1.3.0.2
# 2. Inject the DLL:
Injector/Release/GameHack.exe ac_client.exe "C:\Users\JJ\Documents\hacking\GameHack\Release\Injector.dll"
```

## Learning Path Completed So Far
1. **Memory scanning** — Found player struct offsets with Cheat Engine
2. **DLL injection** — Built a CreateRemoteThread + LoadLibraryA injector
3. **Internal hack DLL** — ESP overlay via OpenGL hook + aimbot with smooth aim
4. **Reverse engineering** — Ghidra walkthrough analyzing our own compiled DLL

## Next Steps (Potential)
- Anti-cheat concepts: how would you detect this hack?
- Kernel-mode drivers for memory access
- Manual mapping (avoid LoadLibrary detection)
- Pattern scanning (survive game updates)
- Analyzing real anti-cheat systems
