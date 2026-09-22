#include "Remote.h"
#include "State.h"
#include <windows.h>

static HANDLE g_remoteProcess = NULL;
static HANDLE g_remoteStdinWrite = NULL;
static HANDLE g_remoteStdoutRead = NULL;

void SendRemoteCmd(const std::string& cmd) {
    if (!g_remoteMode || !g_remoteStdinWrite) return;
    std::string line = cmd + "\n";
    DWORD written = 0;
    WriteFile(g_remoteStdinWrite, line.c_str(), (DWORD)line.size(), &written, NULL);
}

bool StartRemoteMode(const std::string& host, const std::string& remoteExePath) {
    if (g_remoteMode) StopRemoteMode();

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE stdinRead = NULL, stdinWrite = NULL, stdoutRead = NULL, stdoutWrite = NULL;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) return false;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) {
        CloseHandle(stdinRead); CloseHandle(stdinWrite);
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "ssh " + host + " \"" + remoteExePath + " --pipe-stdin\"";
    if (!CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(stdinRead); CloseHandle(stdinWrite);
        CloseHandle(stdoutRead); CloseHandle(stdoutWrite);
        return false;
    }
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    CloseHandle(pi.hThread);

    g_remoteProcess = pi.hProcess;
    g_remoteStdinWrite = stdinWrite;
    g_remoteStdoutRead = stdoutRead;
    g_remoteMode = true;
    g_remoteHost = host;

    SendRemoteCmd("activate");
    return true;
}

void StopRemoteMode() {
    if (!g_remoteMode) return;
    SendRemoteCmd("deactivate");
    if (g_remoteStdinWrite) { CloseHandle(g_remoteStdinWrite); g_remoteStdinWrite = NULL; }
    if (g_remoteStdoutRead) { CloseHandle(g_remoteStdoutRead); g_remoteStdoutRead = NULL; }
    if (g_remoteProcess) {
        TerminateProcess(g_remoteProcess, 0);
        CloseHandle(g_remoteProcess);
        g_remoteProcess = NULL;
    }
    g_remoteMode = false;
    g_remoteHost.clear();
    g_remoteLastKey[0] = '\0';
}
