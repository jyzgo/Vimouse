#include "ScreenOCR.h"
#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <cstdio>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")

// 辅助：截取屏幕区域保存为 BMP 文件
static bool CaptureScreenRegion(int x, int y, int w, int h, const std::wstring& filePath) {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HGDIOBJ oldBitmap = SelectObject(hMemDC, hBitmap);

    BitBlt(hMemDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

    SelectObject(hMemDC, oldBitmap);

    // 保存为 BMP
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h; // 顶部在前
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    int rowSize = ((w * 3 + 3) & ~3);
    int imageSize = rowSize * h;
    bi.biSizeImage = imageSize;

    BITMAPFILEHEADER bf = {};
    bf.bfType = 0x4D42; // 'BM'
    bf.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageSize;
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::vector<BYTE> pixels(imageSize);
    GetDIBits(hMemDC, hBitmap, 0, h, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    file.write((char*)&bf, sizeof(bf));
    file.write((char*)&bi, sizeof(bi));
    file.write((char*)pixels.data(), imageSize);
    file.close();

    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    return true;
}

// 辅助：运行 PowerShell OCR 命令并获取输出
static std::string RunPowerShellOCR(const std::wstring& bmpPath, int offsetX, int offsetY) {
    // 构建 PowerShell 脚本
    // 将宽字符路径转为 UTF-8
    int pathLen = WideCharToMultiByte(CP_UTF8, 0, bmpPath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string pathUtf8(pathLen - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, bmpPath.c_str(), -1, &pathUtf8[0], pathLen, NULL, NULL);

    std::string psScript =
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
        "[Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null\n"
        "[Windows.Graphics.Imaging.BitmapDecoder,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null\n"
        "\n"
        "$asyncMethods = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {\n"
        "    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.IsGenericMethod\n"
        "}\n"
        "function Await($WinRtTask, $ResultType) {\n"
        "    $asTask = $asyncMethods | Where-Object {\n"
        "        $_.MakeGenericMethod($ResultType)\n"
        "    } | Select-Object -First 1\n"
        "    $netTask = $asTask.MakeGenericMethod($ResultType).Invoke($null, @($WinRtTask))\n"
        "    $netTask.Wait(-1) | Out-Null\n"
        "    $netTask.Result\n"
        "}\n"
        "\n"
        "$file = [System.IO.File]::OpenRead('" + pathUtf8 + "')\n"
        "$stream = [Windows.Storage.Streams.RandomAccessStream,Windows.Foundation,ContentType=WindowsRuntime]\n"
        "$inputStream = [System.IO.WindowsRuntimeStreamExtensions]::AsRandomAccessStream($file)\n"
        "$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($inputStream)) ([Windows.Graphics.Imaging.BitmapDecoder])\n"
        "$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])\n"
        "\n"
        "$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()\n"
        "$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])\n"
        "\n"
        "$lines = @()\n"
        "foreach ($line in $result.Lines) {\n"
        "    foreach ($word in $line.Words) {\n"
        "        $r = $word.BoundingRect\n"
        "        $obj = @{\n"
        "            text = $word.Text\n"
        "            rect = @(\n"
        "                [int]($r.X + " + std::to_string(offsetX) + "),\n"
        "                [int]($r.Y + " + std::to_string(offsetY) + "),\n"
        "                [int]($r.X + $r.Width + " + std::to_string(offsetX) + "),\n"
        "                [int]($r.Y + $r.Height + " + std::to_string(offsetY) + ")\n"
        "            )\n"
        "        }\n"
        "        $lines += $obj\n"
        "    }\n"
        "}\n"
        "$file.Close()\n"
        "$json = $lines | ConvertTo-Json -Compress\n"
        "if ($null -eq $json) { Write-Host '[]' } else { Write-Host $json }\n";

    // 写入临时 ps1 文件
    std::wstring psPath = bmpPath + L".ps1";
    {
        std::ofstream ps(psPath);
        ps << psScript;
    }

    // 构建命令行
    int psPathLen = WideCharToMultiByte(CP_UTF8, 0, psPath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string psPathUtf8(psPathLen - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, psPath.c_str(), -1, &psPathUtf8[0], psPathLen, NULL, NULL);

    std::string cmdLine = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + psPathUtf8 + "\"";

    // 创建管道读取输出
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessA(
        NULL, (LPSTR)cmdLine.c_str(),
        NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
        &si, &pi
    );

    CloseHandle(hWritePipe);

    std::string output;
    if (created) {
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buf[bytesRead] = '\0';
            output += buf;
        }
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hReadPipe);

    // 清理临时文件
    DeleteFileW(bmpPath.c_str());
    DeleteFileW(psPath.c_str());

    // 去掉尾部换行
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    return output;
}

std::string ScanRegion(int x1, int y1, int x2, int y2) {
    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) return "ERR invalid region";
    if (w > 3000 || h > 3000) return "ERR region too large";

    // 截图到临时文件
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring bmpPath = std::wstring(tempPath) + L"vimouse_ocr.bmp";

    if (!CaptureScreenRegion(x1, y1, w, h, bmpPath)) {
        return "ERR failed to capture screen";
    }

    std::string ocrResult = RunPowerShellOCR(bmpPath, x1, y1);

    if (ocrResult.empty() || ocrResult == "null") {
        return "OK []";
    }

    return "OK " + ocrResult;
}

std::string ReadAtCursor(int width, int height) {
    POINT p;
    GetCursorPos(&p);
    return ReadAt(p.x, p.y, width, height);
}

std::string ReadAt(int x, int y, int width, int height) {
    int x1 = x - width / 2;
    int y1 = y - height / 2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    return ScanRegion(x1, y1, x1 + width, y1 + height);
}
