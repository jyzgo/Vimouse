#include "PipeServer.h"
#include "CommandHandler.h"
#include <thread>
#include <fstream>

static std::thread* g_pipeThread = nullptr;
static bool g_pipeRunning = false;

// Pipe 服务端线程：循环接受连接、读命令、执行、返回结果
static void PipeServerThread() {
    while (g_pipeRunning) {
        // 创建命名管道实例
        HANDLE hPipe = CreateNamedPipeW(
            VIMOUSE_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,  // 输出缓冲
            4096,  // 输入缓冲
            1000,  // 默认超时
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            DebugLog("PipeServer: CreateNamedPipe failed, error=%d\n", GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // 等待客户端连接
        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe);
            continue;
        }

        if (!g_pipeRunning) {
            CloseHandle(hPipe);
            break;
        }

        // 读取命令
        char buffer[4096] = {};
        DWORD bytesRead = 0;
        BOOL success = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);

        if (success && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string command(buffer);

            // 去掉尾部换行
            while (!command.empty() && (command.back() == '\n' || command.back() == '\r'))
                command.pop_back();

            // 执行命令
            std::string result = HandleCommand(command);

            // 加换行
            result += "\n";

            // 写回结果
            DWORD bytesWritten = 0;
            WriteFile(hPipe, result.c_str(), (DWORD)result.size(), &bytesWritten, NULL);
            FlushFileBuffers(hPipe);
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

void StartPipeServer() {
    if (g_pipeThread) return;
    g_pipeRunning = true;
    g_pipeThread = new std::thread(PipeServerThread);
}

void StopPipeServer() {
    g_pipeRunning = false;
    // 创建一个虚拟连接来唤醒阻塞在 ConnectNamedPipe 上的线程
    HANDLE hDummy = CreateFileW(
        VIMOUSE_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );
    if (hDummy != INVALID_HANDLE_VALUE) {
        CloseHandle(hDummy);
    }
    if (g_pipeThread && g_pipeThread->joinable()) {
        g_pipeThread->join();
        delete g_pipeThread;
        g_pipeThread = nullptr;
    }
}

// CLI 客户端模式
int RunCLIClient(const std::string& command) {
    HANDLE hPipe = CreateFileW(
        VIMOUSE_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERR cannot connect to Vimouse (is it running?)\n");
        return 1;
    }

    // 设置消息模式
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

    // 发送命令
    std::string cmd = command + "\n";
    DWORD bytesWritten = 0;
    WriteFile(hPipe, cmd.c_str(), (DWORD)cmd.size(), &bytesWritten, NULL);

    // 读取结果
    char buffer[4096] = {};
    DWORD bytesRead = 0;
    ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
    CloseHandle(hPipe);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        printf("%s", buffer);
        // 如果结果以 ERR 开头，返回非零
        return (strncmp(buffer, "ERR", 3) == 0) ? 1 : 0;
    }

    return 1;
}

// CLI 脚本模式
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
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;
        // 去掉首尾空格
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        int ret = RunCLIClient(line);
        if (ret != 0) {
            fprintf(stderr, "Script error at line %d: %s\n", lineNum, line.c_str());
            return ret;
        }
    }
    return 0;
}
