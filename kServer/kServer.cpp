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

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 4096

class RemoteTerminalServer {
private:
    WSADATA wsaData;
    SOCKET ListenSocket;
    bool initialized;
    std::string currentDirectory;

public:
    RemoteTerminalServer() : ListenSocket(INVALID_SOCKET), initialized(false) {
        // Get initial working directory
        char buffer[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buffer);
        currentDirectory = std::string(buffer);
    }

    ~RemoteTerminalServer() {
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

    bool handleCdCommand(const std::string& command) {
        // remove slashes at the end
        while (*currentDirectory.rbegin() == '\\')
        {
            currentDirectory.pop_back();
        }

        // Check if this is a cd command
        if (command.length() >= 2 && command.substr(0, 2) == "cd") {
            std::string targetDir;
            
            if (command.length() == 2) {
                // Just "cd" - go to user's home directory
                char* userProfile = getenv("USERPROFILE");
                if (userProfile) {
                    targetDir = std::string(userProfile);
                } else {
                    targetDir = "C:\\";
                }
            } else if (command.length() > 3 && command[2] == ' ') {
                // "cd <path>"
                targetDir = command.substr(3);
                
                // Remove quotes if present
                if (targetDir.front() == '"' && targetDir.back() == '"') {
                    targetDir = targetDir.substr(1, targetDir.length() - 2);
                }
                
                // Handle relative paths
                if (targetDir[0] != '\\' && (targetDir.length() < 2 || targetDir[1] != ':')) {
                    // It's a relative path, combine with current directory
                    if (targetDir == "..") {
                        // Go up one directory
                        size_t lastSlash = currentDirectory.find_last_of("\\");
                        if (lastSlash != std::string::npos && lastSlash >= 2) { // Don't go above drive root
                            targetDir = currentDirectory.substr(0, lastSlash);
                        } else {
                            targetDir = currentDirectory; // Already at root
                        }
                    } else if (targetDir == ".") {
                        // Stay in current directory
                        targetDir = currentDirectory;
                    } else {
                        // Relative path
                        targetDir = currentDirectory + "\\" + targetDir;
                    }
                }
            } else {
                return false; // Not a valid cd command
            }

            // add slashes at the end
            if (*targetDir.rbegin() != '\\')
            {
                targetDir.push_back('\\');
            }

            // Try to change directory
            if (SetCurrentDirectoryA(targetDir.c_str())) {
                currentDirectory = targetDir;
                return true;
            } else {
                return false; // Failed to change directory
            }
        }
        return false; // Not a cd command
    }

    std::string executeCommand(const std::string& command) {
        std::string result;
        
        // Handle cd commands specially
        if (handleCdCommand(command)) {
            char buffer[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, buffer);
            currentDirectory = std::string(buffer);
            return std::string(buffer) + "\n";
        } else if (command.length() >= 2 && command.substr(0, 2) == "cd") {
            // cd command that failed
            return "The system cannot find the path specified.\n";
        }
        
        // Handle pwd command (show current directory)
        if (command == "pwd") {
            return currentDirectory + "\n";
        }
        
        // For all other commands, ensure we're in the right directory
        SetCurrentDirectoryA(currentDirectory.c_str());
        
        // Use _popen to execute command and capture output
        FILE* pipe = _popen(command.c_str(), "r");
        if (!pipe) {
            return "Error: Could not execute command\n";
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            result += buffer;
        }

        int exitCode = _pclose(pipe);
        
        if (result.empty()) {
            if (exitCode == 0) {
                result = "Command executed successfully (no output)\n";
            } else {
                result = "Command failed with exit code: " + std::to_string(exitCode) + "\n";
            }
        }

        return result;
    }

    void handleClient(SOCKET ClientSocket) {
        char recvbuf[DEFAULT_BUFLEN];
        int recvbuflen = DEFAULT_BUFLEN;

        printf("Client connected\n");

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
                    std::string response = "Goodbye!\n";
                    send(ClientSocket, response.c_str(), (int)response.length(), 0);
                    break;
                }

                // Execute the command
                std::string output = executeCommand(command);
                
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
