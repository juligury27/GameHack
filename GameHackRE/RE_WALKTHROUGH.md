# Reverse Engineering Injector.dll in Ghidra — Walkthrough

**Target:** `C:\Users\JJ\Documents\hacking\GameHack\Release\Injector.dll`
**Source (for comparison):** `C:\Users\JJ\Documents\hacking\GameHack\dllmain.cpp`
**Binary details:** 22KB, PE32, x86 (32-bit), compiled with MSVC

---

## IMPORTANT: PDB File Was Found!

Ghidra automatically loaded `Injector.pdb` (the debug symbols file next to your DLL).
This means **all function names, variable names, and type information are already present**.
You'll see `HackThread`, `InstallHook`, `DrawESP`, etc. directly — not `FUN_XXXXXXXX`.

This is a huge advantage for learning! You can:
- See the real names and compare with source code immediately
- Understand how the decompiler reconstructs logic even with full symbols
- Later, try the **exercise without PDB** (Part 14) to simulate real-world RE

---

## Part 0 — Launch Ghidra and Open the Pre-Analyzed Project

The DLL has already been imported and fully analyzed via headless mode.

1. **Run** `C:\Users\JJ\Documents\hacking\GameHackRE\launch_ghidra.bat`
   - Or run `C:\ghidra\ghidraRun.bat` directly
2. **File > Open Project** > navigate to `C:\Users\JJ\Documents\hacking\GameHackRE`
   - Select `GameHackRE.gpr`
3. **Double-click** `Injector.dll` in the project window to open CodeBrowser
4. Everything is already analyzed — you're ready to explore!

**Layout orientation:**
- **Left panel:** Symbol Tree (functions, imports, exports, labels)
- **Center panel:** Listing (raw assembly with addresses)
- **Right panel:** Decompiler (C-like pseudocode — this is your main view)

---

## Part 1 — Find DllMain (The Entry Point)

### Where to look
- **Symbol Tree** (left panel) > **Exports** > look for `DllMain` or `_DllMain@12`
- OR: **Symbol Tree > Functions > entry** (Ghidra marks the PE entry point)

### What you'll see in the Decompiler

Since the PDB was loaded, function names are visible:

```c
// Ghidra's decompilation of DllMain (with PDB symbols)
BOOL __stdcall DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == 1) {    // DLL_PROCESS_ATTACH = 1
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HackThread, hModule, 0, NULL);
    }
    return TRUE;
}
```

### Compare with source (dllmain.cpp:971-983)

```cpp
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)HackThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
```

### What to notice
- The `switch` statement becomes an `if` — the compiler optimized it since only one case matters
- `DLL_PROCESS_ATTACH` (the constant 1) appears as a literal `1`
- With PDB: `HackThread` appears by name! Without PDB it would be `FUN_XXXXXXXX`
- `DisableThreadLibraryCalls` and `CreateThread` keep their names (imported from kernel32.dll)

---

## Part 2 — Find HackThread (Follow the CreateThread Call)

### How to get there
From DllMain, **double-click** the `FUN_XXXXXXXX` passed to `CreateThread`. That's `HackThread`.

### What you'll see
A long function with:
- `AllocConsole()` call near the top
- `freopen_s` call (opening "CONOUT$")
- `GetModuleHandleA("ac_client.exe")` — the string is visible!
- A big `while(true)` loop with many `GetAsyncKeyState` calls
- At the end: `FreeConsole()`, `FreeLibraryAndExitThread`

### Key things to identify

| Source code | What Ghidra shows |
|---|---|
| `AllocConsole()` | `AllocConsole()` — name preserved (Win32 API import) |
| `GetModuleHandleA("ac_client.exe")` | String `"ac_client.exe"` visible in decompiler |
| `g_baseAddress = (uintptr_t)GetModuleHandle...` | Assignment to a global variable (DAT_XXXXXXXX) |
| `InstallHook()` | Call to `FUN_XXXXXXXX()` — rename it! |
| `GetAsyncKeyState(VK_END)` | `GetAsyncKeyState(0x23)` — VK_END = 0x23 |
| `GetAsyncKeyState(VK_INSERT)` | `GetAsyncKeyState(0x2D)` — VK_INSERT = 0x2D |
| `GetAsyncKeyState(VK_F1)` through `VK_F8` | `GetAsyncKeyState(0x70)` through `0x77` |
| `Sleep(10)` | `Sleep(10)` — preserved |
| `FreeLibraryAndExitThread(hModule, 0)` | Name preserved (API import) |

### Virtual key codes you'll see
```
VK_END    = 0x23     VK_INSERT = 0x2D
VK_F1     = 0x70     VK_F2     = 0x71
VK_F3     = 0x72     VK_F4     = 0x73
VK_F5     = 0x74     VK_F6     = 0x75
VK_F7     = 0x76     VK_F8     = 0x77
VK_SHIFT  = 0x10     VK_RBUTTON = 0x02
```

### Global variables
The boolean toggles (`g_espEnabled`, `g_aimbotEnabled`, etc.) become unnamed globals:
- `DAT_XXXXXXXX` — you'll see `1` and `0` being written to them
- The `cout << "[*] ESP: " << ...` strings help you identify which global is which

---

## Part 3 — Find Functions via String Search (The #1 RE Technique)

### How to search
**Search > For Strings** (or `Ctrl+Shift+S`)
- This shows ALL string literals embedded in the binary

### Strings to search for and what they lead to

| String | Found in function | Maps to source |
|---|---|---|
| `"ac_client.exe"` | HackThread | Line 857 |
| `"opengl32.dll"` | InstallHook | Line 795 |
| `"wglSwapBuffers"` | InstallHook | Line 802 |
| `"[+] wglSwapBuffers at: 0x"` | InstallHook | Line 809 |
| `"[+] wglSwapBuffers hooked successfully!"` | InstallHook | Line 825 |
| `"[+] Hook removed."` | RemoveHook | Line 839 |
| `"GameHack ESP"` | HackThread | Line 853 |
| `"[*] Ejecting..."` | HackThread | Line 958 |
| `"CONOUT$"` | HackThread | Line 850 |
| `"Consolas"` | InitFont | Line 340 |
| `"[*] ESP: "` | HackThread keybind handler | Line 898 |
| `"[*] Aimbot: "` | HackThread keybind handler | Line 922 |
| `"??? [%d]"` | DrawESP | Line 710 |
| `"%s [%d]"` | DrawESP | Line 708 |

### How to follow a string to its function
1. Double-click a string in the Strings window — takes you to the `.rdata` section
2. In the **Listing** (center panel), the string shows **XREF** annotations
3. Click the XREF — jumps to the function that references this string
4. The Decompiler (right panel) updates to show the function

This is exactly how real reverse engineers find interesting code in binaries they've never seen before.

---

## Part 4 — Read InstallHook (The Hook Installation)

### Find it
Search for string `"wglSwapBuffers"` > follow XREF > you're in InstallHook

### What you'll see

```c
// Ghidra's approximate decompilation
void InstallHook(void)
{
    HMODULE hOpenGL;
    void *hookTarget;
    DWORD oldProtect;

    hOpenGL = GetModuleHandleA("opengl32.dll");
    if (hOpenGL == NULL) { /* error path */ return; }

    hookTarget = GetProcAddress(hOpenGL, "wglSwapBuffers");
    if (hookTarget == NULL) { /* error path */ return; }

    // Save original 5 bytes
    memcpy(&DAT_XXXXXXXX, hookTarget, 5);

    // Make memory writable
    VirtualProtect(hookTarget, 5, 0x40, &oldProtect);  // 0x40 = PAGE_EXECUTE_READWRITE

    // Write the JMP instruction
    *(byte *)hookTarget = 0xE9;    // <-- THE HOOK! JMP opcode
    *(uint *)((byte *)hookTarget + 1) = (uint)FUN_XXXXXXXX - (uint)hookTarget - 5;

    VirtualProtect(hookTarget, 5, oldProtect, &oldProtect);
}
```

### Key observations
- `PAGE_EXECUTE_READWRITE` appears as `0x40` — the compiler inlines the constant
- The `0xE9` byte is the x86 JMP rel32 opcode — this is THE hook mechanism
- The math `target - source - 5` calculates the relative jump offset
- `FUN_XXXXXXXX` in the JMP calculation is `hkWglSwapBuffers` — rename it!
- `DAT_XXXXXXXX` where 5 bytes are saved is `g_originalBytes`

### Compare with source (dllmain.cpp:792-827)
The structure maps almost exactly. The compiler preserves the logic but removes all variable names.

---

## Part 5 — Read hkWglSwapBuffers (The Per-Frame Hook)

### Find it
It's the `FUN_XXXXXXXX` that InstallHook writes into the JMP.

### What you'll see
A short function:
```c
BOOL __stdcall hkWglSwapBuffers(HDC hdc)
{
    if (DAT_XXXXXXXX == 0)     // g_fontInitialized
        FUN_XXXXXXXX(hdc);     // InitFont
    FUN_XXXXXXXX();             // DrawESP
    return CallOriginalSwapBuffers(hdc);
}
```

### The call chain
```
DllMain → CreateThread(HackThread)
  HackThread → InstallHook()
    InstallHook patches wglSwapBuffers to JMP to → hkWglSwapBuffers
      hkWglSwapBuffers → InitFont (once), DrawESP (every frame)
        DrawESP → RunAimbot, Begin2D, DrawBox/Line/Circle, End2D
```

---

## Part 6 — Read WorldToScreen (The Math Function)

### Find it
Called from within DrawESP. Look for a function with heavy math: `sinf`, `cosf`, `tanf`.

### What Ghidra shows vs source

The math is the hardest to read in decompiled code. You'll see:

```c
// Ghidra output (approximate)
bool WorldToScreen(float param_1, float param_2, float param_3, ...)
{
    float fVar1, fVar2;

    fVar1 = param_1 - param_4;   // dx = targetX - camX
    fVar2 = param_2 - param_5;   // dy = targetY - camY
    ...

    // The PI/180 conversion appears as: 0.01745329 (PI/180 precomputed)
    // Or: param * 3.141593 / 180.0

    local_8 = sinf(local_c);     // sy = sinf(yr)
    local_10 = cosf(local_c);    // cy = cosf(yr)
    ...

    // The 0.1f comparison becomes: if (fVar < 0.1)
    // tanf(fov * 0.5 * PI / 180.0) is the perspective divide
}
```

### Constants to look for
| Source constant | Appears in Ghidra as |
|---|---|
| `PI = 3.14159265358979f` | `0x40490fdb` (IEEE 754 hex) or `3.141593` |
| `PI / 180.0f` | `0.01745329` (precomputed by compiler) |
| `180.0f / PI` | `57.29578` (precomputed by compiler) |
| `0.1f` | `0.1` (the "behind camera" threshold) |
| `0.5f` | `0.5` (half FOV) |

---

## Part 7 — Read RunAimbot (Entity Loop + Angle Calc)

### Find it
Called from DrawESP, before the Begin2D() call. Has a loop from 0 to 32.

### Structure to identify

```
for (i = 0; i < 32; i++)          // entity loop (Ghidra: local_XX < 0x20)
{
    entityAddr = *(entityList + i*4)  // read pointer from entity list

    if (entityAddr == localPlayer)    // skip self
        continue;

    health = *(entityAddr + 0xEC)     // PLAYER_HEALTH offset
    if (health <= 0) continue;        // skip dead

    team = *(entityAddr + 0x30C)      // PLAYER_TEAM offset
    if (team == localTeam) continue;  // skip teammates

    // Read foot position at offsets 0x28, 0x2C, 0x30
    // Add 4.75 to Z for head height

    // Call CalcAngle (FUN_XXXXXXXX)
    // Compare angle distance to g_aimbotFOV (30.0)

    // If best target found, smooth the aim:
    //   newYaw = yaw + deltaYaw / smoothFactor
    //   Write to *(localPlayer + 0x34)  // PLAYER_YAW
    //   Write to *(localPlayer + 0x38)  // PLAYER_PITCH
}
```

### Key offsets visible in the decompiler
```
0xEC   = PLAYER_HEALTH     (health check)
0x30C  = PLAYER_TEAM       (team check)
0x28   = PLAYER_POS_X      (foot position)
0x2C   = PLAYER_POS_Y
0x30   = PLAYER_POS_Z
0x34   = PLAYER_YAW        (aim write target)
0x38   = PLAYER_PITCH      (aim write target)
0x04   = PLAYER_HEAD_X
0x08   = PLAYER_HEAD_Y
0x0C   = PLAYER_HEAD_Z
```

These offsets are GOLD in real RE — they tell you exactly what memory the cheat accesses.

---

## Part 8 — Read DrawESP (The Rendering Loop)

### Find it
The largest function. Called from hkWglSwapBuffers. Contains the entity loop with OpenGL calls.

### OpenGL function calls you'll see
All imported by name (preserved):
- `glGetIntegerv(GL_VIEWPORT, ...)` — `GL_VIEWPORT = 0xBA2`
- `glMatrixMode(GL_PROJECTION)` — `GL_PROJECTION = 0x1701`
- `glPushMatrix`, `glPopMatrix`, `glLoadIdentity`
- `glOrtho` — sets up 2D drawing
- `glDisable(GL_DEPTH_TEST)` — `GL_DEPTH_TEST = 0xB71`
- `glBegin(GL_LINE_LOOP)` — `GL_LINE_LOOP = 0x02`
- `glBegin(GL_QUADS)` — `GL_QUADS = 0x07`
- `glBegin(GL_LINES)` — `GL_LINES = 0x01`
- `glVertex2f`, `glColor4f`, `glColor3f`
- `glEnd`
- `glRasterPos2f`, `glListBase`, `glCallLists`

### The rendering flow in decompiler
```
1. Read local player pointer from *(base + 0x17E0A8)     // OFFSET_LOCAL_PLAYER
2. Read entity list pointer from *(base + 0x191FCC)       // OFFSET_ENTITY_LIST
3. Read camera position, yaw, pitch, team from local player
4. Call RunAimbot
5. Call Begin2D (sets up orthographic projection)
6. Draw crosshair (two lines at screen center)
7. If aimbot enabled: draw FOV circle
8. For each entity (0..31):
   - Skip self, dead, teammates
   - Call WorldToScreen for head and feet
   - Call DrawBox, DrawFilledBox, DrawLine, DrawText2D
9. Call End2D (restore GL state)
```

---

## Part 9 — Understand SafeRead/SafeWrite (SEH in Decompiler)

### What SEH looks like in Ghidra
`__try/__except` compiles to Structured Exception Handling (SEH). In Ghidra:

- The function has unusual control flow with `_except_handler3` references
- You'll see `__try` blocks wrapped in exception frame setup/teardown
- The decompiler may show it as a normal-looking try/except or as raw SEH setup

### SafeRead template instantiations
Since `SafeRead<T>` is a C++ template, the compiler generates separate copies:
- `SafeRead<uintptr_t>` — reads 4 bytes
- `SafeRead<int>` — reads 4 bytes
- `SafeRead<float>` — reads 4 bytes
- `SafeRead<BYTE>` — reads 1 byte

Each appears as a separate `FUN_XXXXXXXX` in Ghidra. They all look similar:
```c
bool SafeRead_uint(uint address, uint *outValue) {
    // SEH setup
    *outValue = *(uint *)address;
    return true;
    // except: return false;
}
```

---

## Part 10 — Global Offsets: The Crown Jewels

### Search for these hex values in the Listing
**Search > For Scalars** or look in the decompiler output:

| Hex Value | What it is | Where it's used |
|---|---|---|
| `0x17E0A8` | OFFSET_LOCAL_PLAYER | DrawESP (base + offset → local player ptr) |
| `0x191FCC` | OFFSET_ENTITY_LIST | DrawESP (base + offset → entity list ptr) |
| `0xEC` | PLAYER_HEALTH | RunAimbot, DrawESP (health check) |
| `0x30C` | PLAYER_TEAM | RunAimbot, DrawESP (team check) |
| `0x34` | PLAYER_YAW | RunAimbot (write aim), DrawESP (read camera) |
| `0x38` | PLAYER_PITCH | RunAimbot (write aim), DrawESP (read camera) |
| `0x205` | PLAYER_NAME | DrawESP (read name string) |
| `0x28, 0x2C, 0x30` | POS_X, POS_Y, POS_Z | RunAimbot, DrawESP (read position) |

In real reverse engineering, finding these offsets is how you discover what a cheat/anti-cheat accesses in the target process.

---

## Part 11 — What's Lost vs Preserved in Compilation

### GONE (compiler removes these)
- **Variable names** → become `local_XX`, `param_X`, `DAT_XXXXXXXX`
- **Comments** → completely removed
- **Function names** (internal) → become `FUN_XXXXXXXX`
- **Type information** (mostly) → `int`, `float` survive but structs/classes flatten
- **#define constants** → inlined as literal values
- **Template types** → each instantiation is a separate function

### PRESERVED (survives compilation)
- **String literals** → stored in `.rdata` section, fully readable
- **Imported function names** → `GetModuleHandleA`, `VirtualProtect`, `glBegin`, etc.
- **Exported function names** → `DllMain` (and anything in the export table)
- **Numeric constants** → `0x17E0A8`, `0xE9`, `0x30C`, `30.0f`, `5.0f`
- **Control flow** → loops, ifs, switches (restructured but logically equivalent)
- **Call relationships** → who calls whom is fully visible
- **Memory layout** → struct offsets appear as numeric additions

### WHY THIS MATTERS
This is why:
1. **Strings are the #1 RE starting point** — they survive compilation
2. **Offsets/constants are the #2 clue** — they tell you what memory is accessed
3. **Import tables tell you capabilities** — VirtualProtect + GetProcAddress = hook. glBegin = rendering.
4. **Variable names are irrelevant for defense** — don't rely on obfuscating names
5. **String encryption and constant obfuscation** exist specifically to defeat these techniques

---

## Part 12 — Practice Exercises

### Exercise 1: Find the Aimbot FOV Value
1. Search for float constant `30.0` (or hex `0x41F00000`)
2. Find where it's used as a comparison threshold
3. Trace back — it's stored in a global (DAT_XXXXXXXX = g_aimbotFOV)
4. Find the F6/F7 keybind that modifies it (adds/subtracts 5.0)

### Exercise 2: Find the Team Check
1. Search for offset `0x30C` in the decompiler/listing
2. You'll find it in two places: RunAimbot and DrawESP
3. Read the surrounding code: `if (*(BYTE*)(entity + 0x30C) == localTeam) continue;`
4. This is how the hack knows enemy vs teammate

### Exercise 3: Trace the Full Hook Chain
Map the complete execution flow:
```
DllMain
  └→ CreateThread(HackThread)
       └→ HackThread
            ├→ AllocConsole
            ├→ GetModuleHandleA("ac_client.exe")
            ├→ InstallHook
            │    ├→ GetModuleHandleA("opengl32.dll")
            │    ├→ GetProcAddress("wglSwapBuffers")
            │    ├→ memcpy (save original bytes)
            │    ├→ VirtualProtect (make writable)
            │    └→ Write 0xE9 JMP (install hook)
            └→ while(true) { GetAsyncKeyState loop }

[Every frame, game calls wglSwapBuffers which now JMPs to:]
hkWglSwapBuffers
  ├→ InitFont (first call only)
  ├→ DrawESP
  │    ├→ Read local player from base+0x17E0A8
  │    ├→ Read entity list from base+0x191FCC
  │    ├→ RunAimbot (if enabled + RMB held)
  │    │    └→ CalcAngle → SafeWrite yaw/pitch
  │    ├→ Begin2D
  │    ├→ For each entity: WorldToScreen → DrawBox/Line/Text
  │    └→ End2D
  └→ CallOriginalSwapBuffers
       ├→ Restore original 5 bytes
       ├→ Call real wglSwapBuffers
       └→ Re-install JMP hook
```

### Exercise 4: Rename All Functions
Use Ghidra's rename feature (right-click > Rename Function, or press `L`) to rename:

| Ghidra name | Rename to |
|---|---|
| The function CreateThread calls | `HackThread` |
| The function that calls GetProcAddress("wglSwapBuffers") | `InstallHook` |
| The function that restores original bytes at cleanup | `RemoveHook` |
| The function written into the JMP patch | `hkWglSwapBuffers` |
| The function that unhooks-calls-rehooks | `CallOriginalSwapBuffers` |
| The function with sinf/cosf/tanf and perspective math | `WorldToScreen` |
| The function with atan2f and angle calculation | `CalcAngle` |
| The function with the entity loop (0-32) that writes yaw/pitch | `RunAimbot` |
| The large function with glBegin/glEnd calls | `DrawESP` |
| The function that calls glOrtho | `Begin2D` |
| The function that calls glPopMatrix twice | `End2D` |
| Functions that read via SEH (deref + exception handler) | `SafeRead_XXX` |
| The function that calls wglUseFontBitmaps | `InitFont` |
| The function that calls glRasterPos2f + glCallLists | `DrawText2D` |

After renaming everything, the decompiled code becomes almost readable as the original source.

---

## Part 13 — Ghidra Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `G` | Go to address |
| `L` | Rename/Label |
| `T` | Set data type |
| `;` | Add comment |
| `Ctrl+Shift+E` | Search for strings |
| `X` | Show cross-references (XREFs) |
| `Ctrl+E` | Edit function signature |
| `Space` | Toggle Listing/Decompiler focus |
| `Alt+Left` | Navigate back |
| `Alt+Right` | Navigate forward |

---

## Part 14 — Challenge Mode: Strip the PDB (Simulate Real-World RE)

Once you're comfortable navigating with full symbols, try the hard version:

### Setup
1. Copy `Injector.dll` to a new folder WITHOUT the `.pdb` file
2. Create a new Ghidra project and import only the DLL
3. Now all your function names become `FUN_XXXXXXXX` and variables become `DAT_XXXXXXXX`

### The Challenge
Starting from `DllMain` (the only named entry point), can you:
1. Identify `HackThread` purely from the `CreateThread` call and `AllocConsole` inside it?
2. Find `InstallHook` by searching for the string `"wglSwapBuffers"`?
3. Identify the `0xE9` JMP byte write and understand the hook mechanism?
4. Find `DrawESP` by following the OpenGL calls (`glBegin`, `glVertex2f`)?
5. Locate `RunAimbot` by finding the entity loop with offset `0xEC` (health) and `0x30C` (team)?
6. Rename every `FUN_XXXXXXXX` to its correct name using only decompiler output + strings?

This is exactly what you'd do when analyzing a cheat or anti-cheat binary in the wild,
where no PDB exists. The skills transfer directly.

---

## Summary: What You've Learned

1. **Loading a binary** into Ghidra and running auto-analysis
2. **Finding the entry point** (DllMain) and tracing the initialization chain
3. **String search** as the primary technique for finding functions
4. **Reading decompiled code** and mapping it back to source
5. **Identifying constants and offsets** that reveal what memory is accessed
6. **Understanding what survives compilation** (strings, constants, imports, control flow)
7. **Renaming functions** to build understanding incrementally

These are the exact same techniques used to analyze unknown malware, game anti-cheats,
proprietary protocols, and any other closed-source binary. The only difference is that
with unknown binaries, you don't have the source to compare against — you have to
figure out the function names and purpose from context alone.
