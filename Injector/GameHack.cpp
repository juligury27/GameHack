// ============================================================================
// DLL INJECTOR — Educational Purpose
//
// This program injects a DLL into a target process using the classic
// "LoadLibrary" technique. This is the most fundamental injection method
// and works on any process that isn't protected by anti-cheat software.
//
// Usage: Injector.exe <process_name> <dll_path>
// Example: Injector.exe ac_client.exe C:\path\to\GameHack.dll
// ============================================================================

#include <iostream>
#include <string>
#include <windows.h>
#include <tlhelp32.h>  // For CreateToolhelp32Snapshot — lets us enumerate running processes

// ============================================================================
// STEP 1: Find the target process ID (PID) by its name
//
// Every running program has a unique Process ID. We need it to open
// a handle to the game. We use the "Toolhelp32" API to take a snapshot
// of all running processes and search through them by name.
// ============================================================================
DWORD FindProcessId(const std::string& processName)
{
    // Take a snapshot of all currently running processes
    // TH32CS_SNAPPROCESS = include process info in the snapshot
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        std::cerr << "[ERROR] Failed to create process snapshot.\n";
        return 0;
    }

    // PROCESSENTRY32 is a struct that holds info about a single process
    // (name, PID, parent PID, thread count, etc.)
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);  // Windows requires you to set this before use

    // Get the first process in the snapshot
    if (!Process32First(snapshot, &processEntry))
    {
        std::cerr << "[ERROR] Failed to read first process from snapshot.\n";
        CloseHandle(snapshot);
        return 0;
    }

    // Convert our narrow (ASCII) process name to a wide string (WCHAR)
    // because Windows Unicode APIs use wide strings internally.
    // Example: "ac_client.exe" → L"ac_client.exe"
    std::wstring wProcessName(processName.begin(), processName.end());

    // Walk through every process in the snapshot looking for our target
    do
    {
        // processEntry.szExeFile contains the process name as a wide string
        // (e.g. L"ac_client.exe") because this project uses the Unicode character set.
        // _wcsicmp is the wide-string version of case-insensitive compare.
        if (_wcsicmp(processEntry.szExeFile, wProcessName.c_str()) == 0)
        {
            DWORD pid = processEntry.th32ProcessID;
            std::cout << "[+] Found process \"" << processName << "\" with PID: " << pid << "\n";
            CloseHandle(snapshot);
            return pid;
        }
    } while (Process32Next(snapshot, &processEntry));

    // If we get here, we walked through all processes and didn't find it
    CloseHandle(snapshot);
    return 0;
}

// ============================================================================
// STEP 2-6: The injection routine
//
// This is the core of the injector. Once we have the PID, we:
//   2. Open a handle to the target process
//   3. Allocate memory inside the target for our DLL path string
//   4. Write the DLL path into that allocated memory
//   5. Create a thread inside the target that calls LoadLibraryA
//   6. Wait for it to finish, then clean up
// ============================================================================
bool InjectDLL(DWORD processId, const std::string& dllPath)
{
    // ========================================================================
    // STEP 2: Open the target process
    //
    // We need a HANDLE with specific access rights:
    //   PROCESS_ALL_ACCESS gives us full control (read, write, create threads)
    //
    // In a real anti-cheat scenario, this call is often monitored or blocked.
    // For Assault Cube (no anti-cheat), it works fine.
    // ========================================================================
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

    if (!hProcess)
    {
        std::cerr << "[ERROR] Failed to open process. Error code: " << GetLastError() << "\n";
        std::cerr << "        Try running the injector as Administrator.\n";
        return false;
    }
    std::cout << "[+] Opened process handle successfully.\n";

    // ========================================================================
    // STEP 3: Allocate memory inside the target process
    //
    // We need to put the DLL path string somewhere the target process can
    // read it. VirtualAllocEx allocates memory in ANOTHER process's address
    // space (not ours).
    //
    // Parameters:
    //   hProcess         — the target process
    //   nullptr          — let Windows choose the address
    //   dllPath.size()+1 — size needed (path length + null terminator)
    //   MEM_COMMIT | MEM_RESERVE — actually back the memory with physical pages
    //   PAGE_READWRITE   — the target process can read this memory
    // ========================================================================
    size_t pathSize = dllPath.size() + 1;  // +1 for the null terminator '\0'

    LPVOID remoteMem = VirtualAllocEx(
        hProcess,
        nullptr,
        pathSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remoteMem)
    {
        std::cerr << "[ERROR] Failed to allocate memory in target process. Error: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Allocated " << pathSize << " bytes in target process at address: " << remoteMem << "\n";

    // ========================================================================
    // STEP 4: Write the DLL path into the allocated memory
    //
    // Now we copy our DLL's full path (e.g. "C:\hacking\GameHack.dll") into
    // the memory we just allocated inside the game process.
    //
    // After this call, the game's memory contains our DLL path string at
    // the address stored in 'remoteMem'.
    // ========================================================================
    BOOL writeResult = WriteProcessMemory(
        hProcess,
        remoteMem,
        dllPath.c_str(),    // source: our DLL path string
        pathSize,           // number of bytes to write
        nullptr             // we don't need to know how many bytes were written
    );

    if (!writeResult)
    {
        std::cerr << "[ERROR] Failed to write DLL path to target process. Error: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Wrote DLL path to target process memory.\n";

    // ========================================================================
    // STEP 5: Create a remote thread that calls LoadLibraryA
    //
    // This is the key trick. LoadLibraryA is a Windows function that loads
    // a DLL into a process. It takes one argument: a pointer to the DLL path.
    //
    // We already wrote our DLL path into the target's memory (remoteMem).
    // Now we create a new thread INSIDE the target process that calls:
    //
    //     LoadLibraryA("C:\\hacking\\GameHack.dll")
    //
    // Why does this work?
    //   - LoadLibraryA exists in kernel32.dll
    //   - kernel32.dll is loaded at the SAME address in every process
    //   - So the address of LoadLibraryA in OUR process == its address in the game
    //   - We pass remoteMem as the argument (the DLL path we wrote)
    //
    // When this thread runs, Windows loads our DLL into the game, and our
    // DLL's DllMain function gets called. We're in!
    // ========================================================================

    // Get the address of LoadLibraryA from kernel32.dll
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC loadLibAddr = GetProcAddress(hKernel32, "LoadLibraryA");

    if (!loadLibAddr)
    {
        std::cerr << "[ERROR] Failed to find LoadLibraryA address.\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] LoadLibraryA found at address: " << loadLibAddr << "\n";

    // Create the remote thread
    HANDLE hThread = CreateRemoteThread(
        hProcess,
        nullptr,                              // default security
        0,                                    // default stack size
        (LPTHREAD_START_ROUTINE)loadLibAddr,  // thread function = LoadLibraryA
        remoteMem,                            // argument = pointer to our DLL path
        0,                                    // run immediately
        nullptr                               // we don't need the thread ID
    );

    if (!hThread)
    {
        std::cerr << "[ERROR] Failed to create remote thread. Error: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Remote thread created! DLL is being loaded...\n";

    // ========================================================================
    // STEP 6: Wait for the thread to finish, then clean up
    //
    // We wait for LoadLibraryA to finish executing in the target process.
    // After it returns, our DLL is loaded and its DllMain has been called.
    // Then we free the memory we allocated (the DLL path string) since
    // it's no longer needed — the DLL is already loaded.
    // ========================================================================
    WaitForSingleObject(hThread, INFINITE);  // wait until LoadLibraryA returns

    std::cout << "[+] DLL injected successfully!\n";

    // Clean up: free the remote memory and close all handles
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return true;
}

// ============================================================================
// MAIN — Parse arguments and run the injection
// ============================================================================
int main(int argc, char* argv[])
{
    std::cout << "========================================\n";
    std::cout << "  DLL Injector — Educational Purpose\n";
    std::cout << "========================================\n\n";

    // Expect 2 arguments: process name and DLL path
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <process_name> <dll_path>\n";
        std::cout << "Example: " << argv[0] << " ac_client.exe C:\\hacking\\GameHack.dll\n";
        return 1;
    }

    std::string processName = argv[1];
    std::string dllPath = argv[2];

    std::cout << "[*] Target process: " << processName << "\n";
    std::cout << "[*] DLL to inject:  " << dllPath << "\n\n";

    // Step 1: Find the process
    DWORD pid = FindProcessId(processName);
    if (pid == 0)
    {
        std::cerr << "[ERROR] Could not find process \"" << processName << "\". Is it running?\n";
        return 1;
    }

    // Steps 2-6: Inject the DLL
    if (!InjectDLL(pid, dllPath))
    {
        std::cerr << "[ERROR] Injection failed.\n";
        return 1;
    }

    std::cout << "\n[*] Done. Your DLL is now running inside the target process.\n";
    return 0;
}
