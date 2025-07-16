#include "RemoteTerminalClient.h"

RemoteTerminalClient::RemoteTerminalClient() : ConnectSocket(INVALID_SOCKET), connected(false), shouldStop(false) {}

RemoteTerminalClient::~RemoteTerminalClient() {
    cleanup();
}

bool RemoteTerminalClient::initialize() {
    // Initialize Winsock
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return false;
    }
    return true;
}

bool RemoteTerminalClient::connectToServer(const std::string& serverAddress) {
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

bool RemoteTerminalClient::sendCommand(const std::string& command) {
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

void RemoteTerminalClient::continuousReceive() {
    char recvbuf[DEFAULT_BUFLEN];
    std::string buffer;
    const std::string endMarker = END_OF_RESPONSE_MARKER;

    while (!shouldStop && connected) {
        int iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN - 1, 0);
        if (iResult > 0) {
            recvbuf[iResult] = '\0';
            buffer += std::string(recvbuf);
            
            // Process complete messages (those ending with end marker)
            size_t markerPos;
            while ((markerPos = buffer.find(endMarker)) != std::string::npos) {
                std::string message = buffer.substr(0, markerPos);
                buffer = buffer.substr(markerPos + endMarker.length());
                
                // Remove trailing newlines that were added before the marker
                while (!message.empty() && message.back() == '\n') {
                    message.pop_back();
                }
                
                // Thread-safe output
                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    printf("\r%s", message.c_str());
                    if (!message.empty() && message.back() != '\n') {
                        printf("\n");
                    }
                    printf("remote> ");
                    fflush(stdout);
                }
            }
        }
        else if (iResult == 0) {
            // Connection closed
            {
                std::lock_guard<std::mutex> lock(outputMutex);
                printf("\rConnection closed by server\n");
                printf("remote> ");
                fflush(stdout);
            }
            connected = false;
            break;
        }
        else {
            // Error occurred
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    printf("\rReceive failed with error: %d\n", WSAGetLastError());
                    printf("remote> ");
                    fflush(stdout);
                }
                connected = false;
                break;
            }
        }
    }
}

void RemoteTerminalClient::run() {
    if (!connected) {
        printf("Not connected to server\n");
        return;
    }

    printf("\nRemote Terminal Client (Async Mode)\n");
    printf("Type commands to execute on the remote server.\n");
    printf("Responses will appear automatically as they arrive.\n");
    printf("Type 'exit' or 'quit' to disconnect.\n\n");

    // Start the continuous receive thread
    receiveThread = std::thread(&RemoteTerminalClient::continuousReceive, this);

    std::string command;
    while (connected && !shouldStop) {
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            printf("remote> ");
            fflush(stdout);
        }
        
        std::getline(std::cin, command);

        if (command.empty()) {
            continue;
        }

        // Check if user wants to exit
        if (command == "exit" || command == "quit") {
            shouldStop = true;
            if (!sendCommand(command)) {
                break;
            }
            // Give a moment for the exit response to arrive
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        // Send the command to server (asynchronously)
        if (!sendCommand(command)) {
            {
                std::lock_guard<std::mutex> lock(outputMutex);
                printf("Failed to send command\n");
            }
            break;
        }
    }

    // Cleanup: stop the receive thread
    shouldStop = true;
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

void RemoteTerminalClient::cleanup() {
    // Stop the receive thread
    shouldStop = true;
    connected = false;
    
    // Join the receive thread if it's running
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
    
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