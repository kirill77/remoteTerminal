// kServer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#pragma comment(lib, "ws2_32.lib")

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <memory>
#include <thread>
#include <cstdio>
#include "../common.h"

class RemoteTerminalServer {
private:
    WSADATA wsaData;
    SOCKET ListenSocket;
    bool initialized;
    std::string currentDirectory;
    
    // Persistent shell session
    HANDLE hChildStdInRd, hChildStdInWr;
    HANDLE hChildStdOutRd, hChildStdOutWr;
    HANDLE hChildStdErrRd, hChildStdErrWr;
    PROCESS_INFORMATION piProcInfo;
    bool shellActive;

public:
    RemoteTerminalServer() : ListenSocket(INVALID_SOCKET), initialized(false), shellActive(false) {
        // Get initial working directory
        char buffer[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buffer);
        currentDirectory = std::string(buffer);
        
        // Initialize pipe handles
        hChildStdInRd = hChildStdInWr = NULL;
        hChildStdOutRd = hChildStdOutWr = NULL;
        hChildStdErrRd = hChildStdErrWr = NULL;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    }

    ~RemoteTerminalServer() {
        destroyPersistentShell();
        cleanup();
    }

    bool initialize() {
        // Initialize Winsock
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            return false;
        }

        struct addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        // Resolve the server address and port
        struct addrinfo* result = NULL;
        iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
        if (iResult != 0) {
            printf("getaddrinfo failed with error: %d\n", iResult);
            WSACleanup();
            return false;
        }

        // Create a SOCKET for the server to listen for client connections
        ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            printf("socket failed with error: %d\n", WSAGetLastError());
            freeaddrinfo(result);
            WSACleanup();
            return false;
        }

        // Setup the TCP listening socket
        iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            printf("bind failed with error: %d\n", WSAGetLastError());
            freeaddrinfo(result);
            closesocket(ListenSocket);
            WSACleanup();
            return false;
        }

        freeaddrinfo(result);

        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            printf("listen failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return false;
        }

        initialized = true;
        printf("Server initialized and listening on port %s\n", DEFAULT_PORT);
        return true;
    }



    bool createPersistentShell() {
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

    void destroyPersistentShell() {
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

    std::string executeCommand(const std::string& command) {
        if (!shellActive) {
            return "Error: Persistent shell not active\n";
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
            return "Error: Failed to write command to shell\n";
        }

        // Read output from shell
        std::string result;
        char buffer[4096];
        DWORD dwRead;
        
        // Set a timeout for reading
        DWORD timeout = 5000; // 5 seconds
        DWORD startTime = GetTickCount();
        
        while (GetTickCount() - startTime < timeout) {
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

    void handleClient(SOCKET ClientSocket) {
        char recvbuf[DEFAULT_BUFLEN];
        int recvbuflen = DEFAULT_BUFLEN;

        printf("Client connected\n");
        
        // Create persistent shell for this client session
        if (!createPersistentShell()) {
            printf("Failed to create persistent shell for client\n");
            std::string errorResponse = "Error: Failed to initialize shell session\n" END_OF_RESPONSE_MARKER "\n";
            send(ClientSocket, errorResponse.c_str(), (int)errorResponse.length(), 0);
            closesocket(ClientSocket);
            return;
        }

        while (true) {
            // Receive data from client
            int iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
            if (iResult > 0) {
                // Null-terminate the received data
                recvbuf[iResult] = '\0';
                std::string command(recvbuf);
                
                // Remove trailing newline if present
                if (!command.empty() && command.back() == '\n') {
                    command.pop_back();
                }
                if (!command.empty() && command.back() == '\r') {
                    command.pop_back();
                }

                printf("Received command: %s\n", command.c_str());

                // Check for exit command
                if (command == "exit" || command == "quit") {
                    std::string response = "Goodbye!\n" END_OF_RESPONSE_MARKER "\n";
                    send(ClientSocket, response.c_str(), (int)response.length(), 0);
                    break;
                }

                // Execute the command
                std::string output = executeCommand(command);
                
                // Add end-of-response marker
                output += "\n" END_OF_RESPONSE_MARKER "\n";
                
                // Send the output back to client
                int sendResult = send(ClientSocket, output.c_str(), (int)output.length(), 0);
                if (sendResult == SOCKET_ERROR) {
                    printf("send failed with error: %d\n", WSAGetLastError());
                    break;
                }
            }
            else if (iResult == 0) {
                printf("Client disconnected\n");
                break;
            }
            else {
                printf("recv failed with error: %d\n", WSAGetLastError());
                break;
            }
        }

        // Cleanup persistent shell when client disconnects
        destroyPersistentShell();
        
        closesocket(ClientSocket);
        printf("Client connection closed\n");
    }

    void run() {
        if (!initialized) {
            printf("Server not initialized\n");
            return;
        }

        printf("Waiting for client connections...\n");

        while (true) {
            // Accept a client socket
            SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
            if (ClientSocket == INVALID_SOCKET) {
                printf("accept failed with error: %d\n", WSAGetLastError());
                break;
            }

            // Handle client in a separate thread
            std::thread clientThread(&RemoteTerminalServer::handleClient, this, ClientSocket);
            clientThread.detach(); // Let the thread run independently
        }
    }

    void cleanup() {
        if (ListenSocket != INVALID_SOCKET) {
            closesocket(ListenSocket);
        }
        if (initialized) {
            WSACleanup();
        }
    }
};

int main() {
    printf("Remote Terminal Server Starting...\n");

    RemoteTerminalServer server;
    
    if (!server.initialize()) {
        printf("Failed to initialize server\n");
        return 1;
    }

    server.run();
    
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
