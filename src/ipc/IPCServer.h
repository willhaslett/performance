#pragma once
#include <juce_events/juce_events.h>
#include <thread>
#include <atomic>
#include <string>

class LuaEngine;

class IPCServer {
public:
    IPCServer(LuaEngine& lua);
    ~IPCServer();

    void start(const std::string& socketPath = "/tmp/performance.sock");
    void stop();

private:
    LuaEngine& lua;
    std::thread listenThread;
    std::atomic<bool> running { false };
    int serverFd = -1;
    std::string sockPath;

    void listenLoop();
    void handleClient(int clientFd);
};
