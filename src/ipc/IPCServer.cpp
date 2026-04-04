#include "ipc/IPCServer.h"
#include "scripting/LuaEngine.h"
#include "engine/Log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

IPCServer::IPCServer(LuaEngine& lua) : lua(lua) {}

IPCServer::~IPCServer() {
    stop();
}

void IPCServer::start(const std::string& socketPath) {
    sockPath = socketPath;

    // Remove stale socket file
    unlink(sockPath.c_str());

    serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perfLog("[IPC] Failed to create socket\n");
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perfLog("[IPC] Failed to bind socket at %s\n", sockPath.c_str());
        close(serverFd);
        serverFd = -1;
        return;
    }

    if (listen(serverFd, 5) < 0) {
        perfLog("[IPC] Failed to listen\n");
        close(serverFd);
        serverFd = -1;
        return;
    }

    running = true;
    listenThread = std::thread([this] { listenLoop(); });
    perfLog("[IPC] Listening on %s\n", sockPath.c_str());
}

void IPCServer::stop() {
    running = false;
    if (serverFd >= 0) {
        ::shutdown(serverFd, SHUT_RDWR);
        close(serverFd);
        serverFd = -1;
    }
    if (listenThread.joinable())
        listenThread.join();
    if (!sockPath.empty())
        unlink(sockPath.c_str());
}

void IPCServer::listenLoop() {
    while (running) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) break;
        handleClient(clientFd);
        close(clientFd);
    }
}

void IPCServer::handleClient(int clientFd) {
    // Read all input from client
    std::string buffer;
    char chunk[4096];

    while (true) {
        ssize_t n = read(clientFd, chunk, sizeof(chunk));
        if (n <= 0) break;
        buffer.append(chunk, n);

        // Check if we have a complete message
        // Single-line: ends with newline
        // Multi-line: ends with ---END---\n
        if (buffer.find("---END---\n") != std::string::npos ||
            (buffer.find('\n') != std::string::npos &&
             buffer.find("---END---") == std::string::npos)) {
            break;
        }
    }

    // Strip the ---END--- delimiter if present
    auto endPos = buffer.find("---END---");
    if (endPos != std::string::npos)
        buffer = buffer.substr(0, endPos);

    // Trim trailing whitespace
    while (!buffer.empty() && (buffer.back() == '\n' || buffer.back() == '\r'))
        buffer.pop_back();

    if (buffer.empty()) return;

    perfLog("[IPC] Executing: %s\n", buffer.c_str());

    // Execute on the message thread and wait for result
    struct ExecParams {
        LuaEngine* lua;
        std::string* code;
        std::string result;
    };
    ExecParams params { &lua, &buffer, {} };

    juce::MessageManager::getInstance()->callFunctionOnMessageThread(
        [](void* data) -> void* {
            auto* p = static_cast<ExecParams*>(data);
            p->result = p->lua->executeString(*p->code);
            return nullptr;
        },
        &params);

    std::string result = params.result;

    result += "\n";
    write(clientFd, result.c_str(), result.size());
}
