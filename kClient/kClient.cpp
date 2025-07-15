// kClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <cstdio>
#include "../common.h"

class RemoteTerminalClient {
private:
    WSADATA wsaData;
    SOCKET ConnectSocket;
    bool connected;

public:
    RemoteTerminalClient() : ConnectSocket(INVALID_SOCKET), connected(false) {}

    ~RemoteTerminalClient() {
        cleanup();
    }

    bool initialize() {
        // Initialize Winsock
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            return false;
        }
        return true;
    }

    bool connectToServer(const std::string& serverAddress = "127.0.0.1") {
        struct addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Resolve the server address and port
        struct addrinfo* result = NULL;
        int iResult = getaddrinfo(serverAddress.c_str(), DEFAULT_PORT, &hints, &result);
        if (iResult != 0) {
            printf("getaddrinfo failed with error: %d\n", iResult);
            WSACleanup();
            return false;
        }

        // Attempt to connect to an address until one succeeds
        for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
            // Create a SOCKET for connecting to server
            ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (ConnectSocket == INVALID_SOCKET) {
                printf("socket failed with error: %d\n", WSAGetLastError());
                WSACleanup();
                return false;
            }

            // Connect to server
            iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
            if (iResult == SOCKET_ERROR) {
                closesocket(ConnectSocket);
                ConnectSocket = INVALID_SOCKET;
                continue;
            }
            break;
        }

        freeaddrinfo(result);

        if (ConnectSocket == INVALID_SOCKET) {
            printf("Unable to connect to server!\n");
            WSACleanup();
            return false;
        }

        connected = true;
        printf("Connected to server at %s:%s\n", serverAddress.c_str(), DEFAULT_PORT);
        return true;
    }

    bool sendCommand(const std::string& command) {
        if (!connected) {
            printf("Not connected to server\n");
            return false;
        }

        // Send the command
        int iResult = send(ConnectSocket, command.c_str(), (int)command.length(), 0);
        if (iResult == SOCKET_ERROR) {
            printf("send failed with error: %d\n", WSAGetLastError());
            return false;
        }

        return true;
    }

    std::string receiveResponse() {
        if (!connected) {
            return "Error: Not connected to server";
        }

        char recvbuf[DEFAULT_BUFLEN];
        std::string response;
        const std::string endMarker = END_OF_RESPONSE_MARKER;

        // Keep receiving data until we see the end marker
        while (true) {
            int iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN - 1, 0);
            if (iResult > 0) {
                recvbuf[iResult] = '\0';
                response += std::string(recvbuf);
                
                // Check if we've received the end marker
                size_t markerPos = response.find(endMarker);
                if (markerPos != std::string::npos) {
                    // Remove the end marker and everything after it
                    response = response.substr(0, markerPos);
                    // Remove trailing newlines that were added before the marker
                    while (!response.empty() && response.back() == '\n') {
                        response.pop_back();
                    }
                    break;
                }
            }
            else if (iResult == 0) {
                if (response.empty()) {
                    response = "Connection closed by server";
                }
                connected = false;
                break;
            }
            else {
                if (response.empty()) {
                    response = "recv failed with error: " + std::to_string(WSAGetLastError());
                }
                connected = false;
                break;
            }
        }

        return response;
    }

    void run() {
        if (!connected) {
            printf("Not connected to server\n");
            return;
        }

        printf("\nRemote Terminal Client\n");
        printf("Type commands to execute on the remote server.\n");
        printf("Type 'exit' or 'quit' to disconnect.\n\n");

        std::string command;
        while (connected) {
            printf("remote> ");
            std::getline(std::cin, command);

            if (command.empty()) {
                continue;
            }

            // Send the command to server
            if (!sendCommand(command)) {
                break;
            }

            // Check if user wants to exit
            if (command == "exit" || command == "quit") {
                std::string response = receiveResponse();
                printf("%s", response.c_str());
                break;
            }

            // Receive and display the response
            std::string response = receiveResponse();
            if (!connected) {
                printf("%s\n", response.c_str());
                break;
            }

            printf("%s", response.c_str());
            
            // Add extra newline if response doesn't end with one
            if (!response.empty() && response.back() != '\n') {
                printf("\n");
            }
        }
    }

    void cleanup() {
        if (ConnectSocket != INVALID_SOCKET) {
            // Shutdown the connection
            int iResult = shutdown(ConnectSocket, SD_SEND);
            if (iResult == SOCKET_ERROR) {
                printf("shutdown failed with error: %d\n", WSAGetLastError());
            }
            closesocket(ConnectSocket);
        }
        WSACleanup();
    }
};

int main(int argc, char* argv[]) {
    printf("Remote Terminal Client Starting...\n");

    RemoteTerminalClient client;
    
    if (!client.initialize()) {
        printf("Failed to initialize client\n");
        return 1;
    }

    // Default to localhost, or use command line argument for server address
    std::string serverAddress = "127.0.0.1";
    if (argc > 1) {
        serverAddress = argv[1];
    }

    if (!client.connectToServer(serverAddress)) {
        printf("Failed to connect to server\n");
        return 1;
    }

    client.run();
    
    printf("Client shutting down...\n");
    return 0;
}

