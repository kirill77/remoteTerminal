#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

class PersistentShell {
private:
    HANDLE hChildStdInRd, hChildStdInWr;
    HANDLE hChildStdOutRd, hChildStdOutWr;
    HANDLE hChildStdErrRd, hChildStdErrWr;
    PROCESS_INFORMATION piProcInfo;
    bool shellActive;
    std::string currentDirectory;

    bool initialize();
    void cleanup();

public:
    PersistentShell(const std::string& workingDir = "");
    ~PersistentShell();

    bool isActive() const;
    bool sendCommand(const std::string& command);
    std::string receiveOutput(DWORD timeoutMs = 5000);
    std::string readAvailableOutput(); // New method for streaming
}; 