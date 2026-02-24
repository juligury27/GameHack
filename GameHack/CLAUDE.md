# GameHack DLL — ESP + Aimbot

## What This Is
An internal hack DLL injected into AssaultCube 1.3.0.2. Hooks `wglSwapBuffers` (OpenGL)
to draw ESP overlays and implements a smooth aimbot with adaptive smoothing.

## Architecture
Single file: `dllmain.cpp` (~984 lines), plus PCH headers.

### Execution Flow
```
DllMain (DLL_PROCESS_ATTACH)
  └→ CreateThread → HackThread
       ├→ AllocConsole (debug output)
       ├→ GetModuleHandleA("ac_client.exe") → g_baseAddress
       ├→ InstallHook()
       │    ├→ GetModuleHandleA("opengl32.dll")
       │    ├→ GetProcAddress("wglSwapBuffers")
       │    ├→ Save original 5 bytes
       │    └→ Write 0xE9 JMP to hkWglSwapBuffers
       └→ Keybind loop (GetAsyncKeyState, Sleep(10))

[Every frame — called by game's OpenGL renderer]
hkWglSwapBuffers(HDC)
  ├→ InitFont (once, via wglUseFontBitmaps)
  ├→ DrawESP()
  │    ├→ Read local player (base + 0x17E0A8)
  │    ├→ Read entity list (base + 0x191FCC)
  │    ├→ RunAimbot() — if enabled + RMB held
  │    ├→ Begin2D() — glOrtho setup
  │    ├→ Entity loop (0..31): WorldToScreen → DrawBox/Line/Text
  │    └→ End2D() — restore GL state
  └→ CallOriginalSwapBuffers()
       ├→ Restore original 5 bytes → call real wglSwapBuffers → re-patch JMP
```

### Hook Method: Unhook-Call-Rehook
Not a trampoline — avoids instruction relocation issues. Instead:
1. Save first 5 bytes of `wglSwapBuffers`
2. Overwrite with `E9 XX XX XX XX` (JMP rel32) to our hook
3. To call original: restore bytes → call → re-patch
4. Safe because OpenGL rendering is single-threaded

### Key Systems

**WorldToScreen** — Manual projection (not glGetFloatv, which has stale HUD matrices at
SwapBuffers time). Builds camera basis vectors from yaw/pitch using the Cube engine's
GL transform order: `glRotatef(pitch, -1,0,0)` then `glRotatef(yaw, 0,1,0)`.

**Aimbot** — "Closest to crosshair" target selection with:
- FOV limit (default 30 degrees, adjustable F6/F7)
- Adaptive smoothing: less smoothing for small angle corrections (fixes long-range inaccuracy)
- Snap threshold: angles < 0.5 degrees snap directly

**SafeRead/SafeWrite** — Template functions using SEH (`__try/__except`) for safe
game memory access. Prevents crashes from reading freed/invalid pointers.

**Bitmap Font** — `wglUseFontBitmaps` creates 256 GL display lists for ASCII rendering.
Called via `glCallLists` for player name/health text.

## Keybinds
| Key | Action |
|---|---|
| INSERT | Toggle ESP on/off |
| F1-F4 | Toggle boxes / names / health bars / snap lines |
| F5 | Toggle aimbot |
| F6/F7 | Aimbot FOV +/- 5 degrees |
| F8 | Aimbot smoothing +1 (SHIFT+F8 = -1) |
| Right Mouse | Aim (hold while aimbot ON) |
| END | Eject DLL |

## Build
- Visual Studio project: `Injector.slnx` / `Injector.vcxproj`
- Config: **Release | x86**
- Output: `Release/Injector.dll` + `Release/Injector.pdb`
- Links: `opengl32.lib` (OpenGL), standard Win32 libs
- PCH: `pch.h` → `framework.h` → `<windows.h>`
