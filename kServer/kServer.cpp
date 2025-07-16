#include <iostream>
#include "RemoteTerminalServer.h"

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
