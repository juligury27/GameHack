# GameHack — Game Security Engineering Lab

Learning game security by building and then reverse engineering game hacks for
[AssaultCube](https://assault.cubers.net/) 1.3.0.2 (an open-source FPS).

This is an educational project for understanding how game cheats work — which is
essential knowledge for building anti-cheat systems and game security defenses.

## Projects

### 1. Injector (`Injector/`)
A classic DLL injector using the `CreateRemoteThread` + `LoadLibraryA` technique.

```
Injector.exe ac_client.exe C:\path\to\GameHack.dll
```

**Concepts:** Process enumeration, `OpenProcess`, `VirtualAllocEx`,
`WriteProcessMemory`, `CreateRemoteThread`

### 2. GameHack DLL (`GameHack/`)
An ESP + Aimbot DLL that hooks OpenGL rendering via `wglSwapBuffers`.

**Features:**
- **ESP** — Enemy boxes, names, health bars, snap lines (world-to-screen projection)
- **Aimbot** — Smooth aim with FOV limit, adaptive smoothing, closest-to-crosshair targeting
- **OpenGL Hook** — Unhook-call-rehook method patching `wglSwapBuffers` with a 5-byte JMP

**Concepts:** DLL injection, API hooking (inline patching), OpenGL rendering,
world-to-screen math, structured exception handling (SEH), memory reading/writing

### 3. Reverse Engineering Walkthrough (`GameHackRE/`)
A guided walkthrough for reverse engineering `GameHack.dll` using
[Ghidra](https://ghidra-sre.org/) — NSA's open-source reverse engineering tool.

Since we have the source code, we can directly compare Ghidra's decompilation output
against the original C++ to learn what survives compilation and what gets lost.

**Topics:** String searching, XREF following, decompiler output reading, identifying
offsets/constants, function renaming, SEH in decompiled code

## AC 1.3.0.2 Offsets

| Offset | Field |
|---|---|
| `base + 0x17E0A8` | Local player pointer |
| `base + 0x191FCC` | Entity list pointer |
| `player + 0x04/08/0C` | Head X/Y/Z |
| `player + 0x28/2C/30` | Foot X/Y/Z |
| `player + 0x34` | Yaw |
| `player + 0x38` | Pitch |
| `player + 0xEC` | Health |
| `player + 0x205` | Name |
| `player + 0x30C` | Team (0=CLA, 1=RVSF) |

## Disclaimer

This project is for **educational purposes only** — learning game security engineering,
reverse engineering, and anti-cheat concepts. Do not use these techniques against online
games with anti-cheat protection or in ways that violate terms of service.
