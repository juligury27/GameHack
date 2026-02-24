# Injector — DLL Injector

## What This Is
A standalone console EXE that injects a DLL into a running process using the classic
`CreateRemoteThread` + `LoadLibraryA` technique. This is the simplest and most common
DLL injection method.

## Architecture
Single file: `GameHack.cpp` (~277 lines)

### Injection Flow
```
1. FindProcessId()     — Enumerate processes via CreateToolhelp32Snapshot, match by name
2. OpenProcess()       — Get PROCESS_ALL_ACCESS handle to target
3. VirtualAllocEx()    — Allocate memory in target for DLL path string
4. WriteProcessMemory() — Write the DLL path into allocated memory
5. CreateRemoteThread() — Create thread in target calling LoadLibraryA(dllPath)
6. WaitForSingleObject() — Wait for LoadLibraryA to complete
7. Cleanup             — VirtualFreeEx + CloseHandle
```

### Why LoadLibraryA Address Works Cross-Process
`kernel32.dll` is loaded at the same base address in every 32-bit process (ASLR applies
per-boot, not per-process for system DLLs). So `GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA")`
gives us an address valid in both our process and the target.

## Build
- Visual Studio project: `GameHack.slnx` / `GameHack.vcxproj`
- Config: **Release | x86** (32-bit, matching AssaultCube)
- Output: `Release/GameHack.exe`
- Unicode character set (wide strings for process enumeration)

## Usage
```
GameHack.exe <process_name> <full_dll_path>
GameHack.exe ac_client.exe "C:\Users\JJ\Documents\hacking\GameHack\Release\Injector.dll"
```
Must run as Administrator if target process has higher privileges.

## Limitations
- Easily detected by any anti-cheat (CreateRemoteThread is heavily monitored)
- No error recovery — if injection fails partway, allocated memory may leak
- LoadLibraryA leaves the DLL visible in the target's module list
- No ASLR bypass needed for 32-bit targets (kernel32 base is shared)
