#include "PersistentShell.h"
#include <iostream>
#include <cstdio>

PersistentShell::PersistentShell(const std::string& workingDir) : shellActive(false) {
    // Initialize pipe handles
    hChildStdInRd = hChildStdInWr = NULL;
    hChildStdOutRd = hChildStdOutWr = NULL;
    hChildStdErrRd = hChildStdErrWr = NULL;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    
    // Set working directory
    if (workingDir.empty()) {
        char buffer[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buffer);
        currentDirectory = std::string(buffer);
    } else {
        currentDirectory = workingDir;
    }
    
    // Create the shell
    if (!initialize()) {
        printf("Failed to initialize persistent shell\n");
    }
}

PersistentShell::~PersistentShell() {
    cleanup();
}

bool PersistentShell::isActive() const {
    return shellActive;
}

bool PersistentShell::sendCommand(const std::string& command) {
    if (!shellActive) {
        return false;
    }

    // Handle pwd command (show current directory) - send to shell
    std::string actualCommand = command;
    if (command == "pwd") {
        actualCommand = "cd";  // cmd.exe's cd without arguments shows current directory
    }

    // Write command to shell's stdin
    std::string cmdWithNewline = actualCommand + "\r\n";
    DWORD dwWritten;
    if (!WriteFile(hChildStdInWr, cmdWithNewline.c_str(), (DWORD)cmdWithNewline.length(), &dwWritten, NULL)) {
        return false;
    }
    return true;
}

std::string PersistentShell::receiveOutput(DWORD timeoutMs) {
    if (!shellActive) {
        return "Error: Persistent shell not active\n";
    }

    // Read output from shell
    std::string result;
    char buffer[4096];
    DWORD dwRead;
    
    // Set a timeout for reading
    DWORD startTime = GetTickCount();
    
    while (GetTickCount() - startTime < timeoutMs) {
        // Check if data is available
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(hChildStdOutRd, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(hChildStdOutRd, buffer, sizeof(buffer) - 1, &dwRead, NULL) && dwRead > 0) {
                buffer[dwRead] = '\0';
                result += buffer;
                
                // Check if we've reached a command prompt (indicating command completion)
                if (result.find(">") != std::string::npos) {
                    // Look for the last occurrence of ">" which should be the prompt
                    size_t lastPrompt = result.rfind(">");
                    if (lastPrompt != std::string::npos) {
                        // Extract everything before the prompt
                        std::string output = result.substr(0, lastPrompt);
                        
                        // Remove the command echo (first line)
                        size_t firstNewline = output.find("\r\n");
                        if (firstNewline != std::string::npos) {
                            output = output.substr(firstNewline + 2);
                        }
                        
                        // Clean up trailing whitespace
                        while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
                            output.pop_back();
                        }
                        
                        return output.empty() ? "Command executed successfully (no output)\n" : output + "\n";
                    }
                }
            }
        }
        
        // Also check stderr
        if (PeekNamedPipe(hChildStdErrRd, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(hChildStdErrRd, buffer, sizeof(buffer) - 1, &dwRead, NULL) && dwRead > 0) {
                buffer[dwRead] = '\0';
                result += buffer;
            }
        }
        
        Sleep(10); // Small delay to prevent busy waiting
    }

    // If we get here, we timed out
    return "Error: Command execution timed out\n";
}

bool PersistentShell::initialize() {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create pipes for stdin
    if (!CreatePipe(&hChildStdInRd, &hChildStdInWr, &saAttr, 0)) {
        printf("CreatePipe failed for stdin\n");
        return false;
    }
    if (!SetHandleInformation(hChildStdInWr, HANDLE_FLAG_INHERIT, 0)) {
        printf("SetHandleInformation failed for stdin\n");
        return false;
    }

    // Create pipes for stdout
    if (!CreatePipe(&hChildStdOutRd, &hChildStdOutWr, &saAttr, 0)) {
        printf("CreatePipe failed for stdout\n");
        return false;
    }
    if (!SetHandleInformation(hChildStdOutRd, HANDLE_FLAG_INHERIT, 0)) {
        printf("SetHandleInformation failed for stdout\n");
        return false;
    }

    // Create pipes for stderr
    if (!CreatePipe(&hChildStdErrRd, &hChildStdErrWr, &saAttr, 0)) {
        printf("CreatePipe failed for stderr\n");
        return false;
    }
    if (!SetHandleInformation(hChildStdErrRd, HANDLE_FLAG_INHERIT, 0)) {
        printf("SetHandleInformation failed for stderr\n");
        return false;
    }

    // Create the child process (cmd.exe)
    STARTUPINFOA siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStdErrWr;
    siStartInfo.hStdOutput = hChildStdOutWr;
    siStartInfo.hStdInput = hChildStdInRd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Start cmd.exe
    char cmdLine[] = "cmd.exe";
    BOOL bSuccess = CreateProcessA(NULL,
        cmdLine,     // command line
        NULL,        // process security attributes
        NULL,        // primary thread security attributes
        TRUE,        // handles are inherited
        0,           // creation flags
        NULL,        // use parent's environment
        currentDirectory.c_str(), // current directory
        &siStartInfo, // STARTUPINFO pointer
        &piProcInfo); // receives PROCESS_INFORMATION

    if (!bSuccess) {
        printf("CreateProcess failed for cmd.exe\n");
        return false;
    }

    // Close handles to the stdin and stdout pipes no longer needed by the child process.
    CloseHandle(hChildStdOutWr);
    CloseHandle(hChildStdInRd);
    CloseHandle(hChildStdErrWr);
    hChildStdOutWr = NULL;
    hChildStdInRd = NULL;
    hChildStdErrWr = NULL;

    shellActive = true;
    printf("Persistent shell created\n");
    return true;
}

void PersistentShell::cleanup() {
    if (!shellActive) return;

    // Send exit command to terminate cmd.exe gracefully
    if (hChildStdInWr) {
        const char* exitCmd = "exit\r\n";
        DWORD dwWritten;
        WriteFile(hChildStdInWr, exitCmd, (DWORD)strlen(exitCmd), &dwWritten, NULL);
        CloseHandle(hChildStdInWr);
        hChildStdInWr = NULL;
    }

    // Wait for process to terminate
    if (piProcInfo.hProcess) {
        WaitForSingleObject(piProcInfo.hProcess, 2000); // Wait up to 2 seconds
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
    }

    // Close remaining handles
    if (hChildStdOutRd) { CloseHandle(hChildStdOutRd); hChildStdOutRd = NULL; }
    if (hChildStdErrRd) { CloseHandle(hChildStdErrRd); hChildStdErrRd = NULL; }
    if (hChildStdInRd) { CloseHandle(hChildStdInRd); hChildStdInRd = NULL; }
    if (hChildStdOutWr) { CloseHandle(hChildStdOutWr); hChildStdOutWr = NULL; }
    if (hChildStdErrWr) { CloseHandle(hChildStdErrWr); hChildStdErrWr = NULL; }

    shellActive = false;
    printf("Persistent shell destroyed\n");
} 