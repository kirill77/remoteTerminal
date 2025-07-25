#pragma once

#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include "../common.h"

class RemoteTerminalClient {
private:
    WSADATA wsaData;
    SOCKET ConnectSocket;
    std::atomic<bool> connected;
    std::atomic<bool> shouldStop;
    std::thread receiveThread;
    std::mutex outputMutex;

    bool sendCommand(const std::string& command);
    void continuousReceive();
    void cleanup();

public:
    RemoteTerminalClient();
    ~RemoteTerminalClient();

    bool initialize();
    bool connectToServer(const std::string& serverAddress = "127.0.0.1");
    void run();
}; 