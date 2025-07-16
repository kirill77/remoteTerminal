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
#include <ctime>
#include "../common.h"
#include "PersistentShell.h"

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


    std::string getCurrentTimestamp() {
        std::time_t rawtime;
        std::time(&rawtime);
        struct tm* timeinfo = std::localtime(&rawtime);
        char buffer[100];
        std::strftime(buffer, sizeof(buffer), "[%H:%M:%S] ", timeinfo);
        return std::string(buffer);
    }

    std::string executeCommand(PersistentShell& shell, const std::string& command) {
        if (!shell.isActive()) {
            return getCurrentTimestamp() + "Error: Shell not active\n";
        }

        if (!shell.sendCommand(command)) {
            return getCurrentTimestamp() + "Error: Failed to send command to shell\n";
        }

        std::string output = shell.receiveOutput();
        
        // Add timestamp prefix to the output
        return getCurrentTimestamp() + output;
    }

    void handleClient(SOCKET ClientSocket) {
        char recvbuf[DEFAULT_BUFLEN];
        int recvbuflen = DEFAULT_BUFLEN;

        printf("Client connected\n");
        
        // Create persistent shell for this client session
        PersistentShell shell(currentDirectory);
        if (!shell.isActive()) {
            printf("Failed to create persistent shell for client\n");
            std::string errorResponse = "Error: Failed to initialize shell session\n" END_OF_RESPONSE_MARKER "\n";
            send(ClientSocket, errorResponse.c_str(), (int)errorResponse.length(), 0);
            closesocket(ClientSocket);
            return;
        }

        // Send welcome message immediately (demonstrates async capability)
        std::string welcomeMessage = getCurrentTimestamp() + "Welcome to Remote Terminal Server!\n" + 
                                   getCurrentTimestamp() + "Shell session initialized.\n" + END_OF_RESPONSE_MARKER + "\n";
        send(ClientSocket, welcomeMessage.c_str(), (int)welcomeMessage.length(), 0);

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
                std::string output = executeCommand(shell, command);
                
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

        // Shell will be automatically destroyed when it goes out of scope
        
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
