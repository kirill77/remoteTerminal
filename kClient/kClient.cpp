// kClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "RemoteTerminalClient.h"

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

