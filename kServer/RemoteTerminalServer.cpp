#include "RemoteTerminalServer.h"

RemoteTerminalServer::RemoteTerminalServer() : ListenSocket(INVALID_SOCKET), initialized(false) {
    // Get initial working directory
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    currentDirectory = std::string(buffer);
}

RemoteTerminalServer::~RemoteTerminalServer() {
    cleanup();
}

bool RemoteTerminalServer::initialize() {
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

std::string RemoteTerminalServer::getCurrentTimestamp() {
    std::time_t rawtime;
    std::time(&rawtime);
    struct tm* timeinfo = std::localtime(&rawtime);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "[%H:%M:%S] ", timeinfo);
    return std::string(buffer);
}

void RemoteTerminalServer::continuousOutputMonitor(SOCKET clientSocket, PersistentShell& shell, std::atomic<bool>& shouldStop) {
    printf("Output monitoring thread started\n");
    
    while (!shouldStop && shell.isActive()) {
        // Read any available output from the shell (non-blocking)
        std::string output = shell.readAvailableOutput();
        
        if (!output.empty()) {
            // Add timestamp prefix to the output
            std::string timestampedOutput = getCurrentTimestamp() + output;
            
            // Send the output to client immediately (no end marker for streaming)
            int sendResult = send(clientSocket, timestampedOutput.c_str(), (int)timestampedOutput.length(), 0);
            if (sendResult == SOCKET_ERROR) {
                printf("Failed to send output to client: %d\n", WSAGetLastError());
                break;
            }
            
            // Log what we sent (but clean it up for display)
            std::string cleanOutput = output;
            while (!cleanOutput.empty() && (cleanOutput.back() == '\r' || cleanOutput.back() == '\n')) {
                cleanOutput.pop_back();
            }
            if (!cleanOutput.empty()) {
                printf("Sent output to client: %s\n", cleanOutput.c_str());
            }
        }
        
        // Small sleep to prevent excessive CPU usage when no output is available
        Sleep(50);
    }
    
    printf("Output monitoring thread ended\n");
}

void RemoteTerminalServer::handleClient(SOCKET ClientSocket) {
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

    // Send welcome message immediately
    std::string welcomeMessage = getCurrentTimestamp() + "Welcome to Remote Terminal Server!\n" + 
                               getCurrentTimestamp() + "Shell session initialized.\n" + END_OF_RESPONSE_MARKER + "\n";
    send(ClientSocket, welcomeMessage.c_str(), (int)welcomeMessage.length(), 0);

    // Start continuous output monitoring thread
    std::atomic<bool> shouldStopMonitoring(false);
    std::thread outputMonitorThread(&RemoteTerminalServer::continuousOutputMonitor, this, 
                                   ClientSocket, std::ref(shell), std::ref(shouldStopMonitoring));

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
                shouldStopMonitoring = true;
                
                // Send the exit command to shell
                if (shell.sendCommand(command)) {
                    // Wait a moment for any final output
                    Sleep(500);
                }
                
                std::string response = "Goodbye!\n" END_OF_RESPONSE_MARKER "\n";
                send(ClientSocket, response.c_str(), (int)response.length(), 0);
                break;
            }

            // Send the command to shell (non-blocking)
            if (!shell.sendCommand(command)) {
                std::string errorResponse = getCurrentTimestamp() + "Error: Failed to send command to shell\n" + END_OF_RESPONSE_MARKER + "\n";
                send(ClientSocket, errorResponse.c_str(), (int)errorResponse.length(), 0);
            }
            // Note: Output will be handled by the monitoring thread
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

    // Stop the monitoring thread
    shouldStopMonitoring = true;
    if (outputMonitorThread.joinable()) {
        outputMonitorThread.join();
    }

    // Shell will be automatically destroyed when it goes out of scope
    
    closesocket(ClientSocket);
    printf("Client connection closed\n");
}

void RemoteTerminalServer::run() {
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

void RemoteTerminalServer::cleanup() {
    if (ListenSocket != INVALID_SOCKET) {
        closesocket(ListenSocket);
    }
    if (initialized) {
        WSACleanup();
    }
} 