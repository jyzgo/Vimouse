#include "PipeServer.h"
#include "CommandHandler.h"
#include <thread>
#include <fstream>
#include <cstdio>
#include <cstring>

static std::thread g_pipeThread;
static volatile bool g_pipeRunning = false;

// 每个连接：读一条命令 → 执行 → 写回一行 → 断开
static void PipeServerThread() {
    while (g_pipeRunning) {
        HANDLE hPipe = CreateNamedPipeW(VIMOUSE_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                        PIPE_UNLIMITED_INSTANCES, 4096, 4096, 1000, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            DebugLog("PipeServer: CreateNamedPipe failed, error=%lu\n", GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) { CloseHandle(hPipe); continue; }
        if (!g_pipeRunning) { CloseHandle(hPipe); break; }

        char buffer[4096] = {};
        DWORD bytesRead = 0;
        if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string command(buffer);
            while (!command.empty() && (command.back() == '\n' || command.back() == '\r')) command.pop_back();

            std::string result = HandleCommand(command) + "\n";
            DWORD written = 0;
            WriteFile(hPipe, result.c_str(), (DWORD)result.size(), &written, NULL);
            FlushFileBuffers(hPipe);
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

void StartPipeServer() {
    if (g_pipeThread.joinable()) return;
    g_pipeRunning = true;
    g_pipeThread = std::thread(PipeServerThread);
}

void StopPipeServer() {
    g_pipeRunning = false;
    // 自己连一下，唤醒阻塞在 ConnectNamedPipe 的线程
    HANDLE hDummy = CreateFileW(VIMOUSE_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDummy != INVALID_HANDLE_VALUE) CloseHandle(hDummy);
    if (g_pipeThread.joinable()) g_pipeThread.join();
}

// 连接管道发送一条命令，结果写到 out。失败返回 false。
static bool SendPipeCommand(const std::string& command, std::string& out) {
    HANDLE hPipe = CreateFileW(VIMOUSE_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

    std::string cmd = command + "\n";
    DWORD written = 0;
    WriteFile(hPipe, cmd.c_str(), (DWORD)cmd.size(), &written, NULL);

    char buffer[4096] = {};
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
    CloseHandle(hPipe);
    if (!ok || bytesRead == 0) return false;
    buffer[bytesRead] = '\0';
    out = buffer;
    return true;
}

int RunCLIClient(const std::string& command) {
    std::string result;
    if (!SendPipeCommand(command, result)) {
        fprintf(stderr, "ERR cannot connect to Vimouse (is it running?)\n");
        return 1;
    }
    printf("%s", result.c_str());
    return (result.rfind("ERR", 0) == 0) ? 1 : 0;
}

int RunPipeStdinBridge() {
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        std::string result;
        if (!SendPipeCommand(line, result)) result = "ERR pipe connect failed\n";
        fputs(result.c_str(), stdout);
        fflush(stdout);
    }
    return 0;
}

int RunCLIScript(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        fprintf(stderr, "ERR cannot open script file: %s\n", filePath.c_str());
        return 1;
    }
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#') continue;
        line = line.substr(start);
        int ret = RunCLIClient(line);
        if (ret != 0) {
            fprintf(stderr, "Script error at line %d: %s\n", lineNum, line.c_str());
            return ret;
        }
    }
    return 0;
}
