#pragma once

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
#include <atomic>
#include <mutex>
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

    void continuousOutputMonitor(SOCKET clientSocket, PersistentShell& shell, std::atomic<bool>& shouldStop);
    std::string getCurrentTimestamp();
    void handleClient(SOCKET ClientSocket);
    void cleanup();

public:
    RemoteTerminalServer();
    ~RemoteTerminalServer();

    bool initialize();
    void run();
}; 