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

// 辅助：获取 JPEG encoder CLSID
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return -1;
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

std::string CaptureScreenJPEG(int x1, int y1, int x2, int y2, int quality) {
    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) return "ERR invalid region";

    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // 截取屏幕
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HGDIOBJ oldBitmap = SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, w, h, hScreenDC, x1, y1, SRCCOPY);
    SelectObject(hMemDC, oldBitmap);

    // 缩放到最大 1920 宽度（节省文件大小）
    int outW = w, outH = h;
    if (outW > 1920) {
        outH = outH * 1920 / outW;
        outW = 1920;
    }

    // 创建 GDI+ Bitmap
    Gdiplus::Bitmap* srcBmp = Gdiplus::Bitmap::FromHBITMAP(hBitmap, NULL);

    Gdiplus::Bitmap* outBmp;
    if (outW != w || outH != h) {
        // 缩放
        outBmp = new Gdiplus::Bitmap(outW, outH, PixelFormat24bppRGB);
        Gdiplus::Graphics g(outBmp);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(srcBmp, 0, 0, outW, outH);
    } else {
        outBmp = srcBmp;
        srcBmp = nullptr; // 避免重复释放
    }

    // 保存为 JPEG
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring jpegPath = std::wstring(tempPath) + L"vimouse_screenshot.jpg";

    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);

    Gdiplus::EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG qualityValue = quality;
    encoderParams.Parameter[0].Value = &qualityValue;

    Gdiplus::Status status = outBmp->Save(jpegPath.c_str(), &jpegClsid, &encoderParams);

    // 清理
    if (srcBmp) delete srcBmp;
    if (outBmp != srcBmp) delete outBmp;
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (status != Gdiplus::Ok) {
        return "ERR failed to save JPEG";
    }

    // 转为 UTF-8 路径
    int pathLen = WideCharToMultiByte(CP_UTF8, 0, jpegPath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string pathUtf8(pathLen - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, jpegPath.c_str(), -1, &pathUtf8[0], pathLen, NULL, NULL);

    return "OK " + pathUtf8;
}

// 辅助：在 GDI+ Bitmap 上绘制坐标网格
static void DrawGrid(Gdiplus::Graphics& g, int w, int h) {
    const int COLS = 26;
    const int ROWS = 14; // 14行足够覆盖屏幕，标签用 A-N
    float cellW = (float)w / COLS;
    float cellH = (float)h / ROWS;

    // 半透明黑色画笔和背景刷
    Gdiplus::Pen gridPen(Gdiplus::Color(60, 255, 255, 255), 1.0f);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(140, 0, 0, 0));
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(230, 0, 255, 100));

    Gdiplus::FontFamily fontFamily(L"Consolas");
    float fontSize = cellH * 0.35f;
    if (fontSize < 8) fontSize = 8;
    if (fontSize > 16) fontSize = 16;
    Gdiplus::Font font(&fontFamily, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 画网格线
    for (int c = 1; c < COLS; c++) {
        g.DrawLine(&gridPen, c * cellW, 0.0f, c * cellW, (float)h);
    }
    for (int r = 1; r < ROWS; r++) {
        g.DrawLine(&gridPen, 0.0f, r * cellH, (float)w, r * cellH);
    }

    // 画标签 (列字母 + 行字母, 如 "AE" = 第1列第5行)
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            wchar_t label[3];
            label[0] = L'A' + c;
            label[1] = L'A' + r;
            label[2] = L'\0';

            float cx = c * cellW + cellW * 0.5f;
            float cy = r * cellH + cellH * 0.5f;

            // 背景框
            float tw = fontSize * 1.6f;
            float th = fontSize * 1.2f;
            Gdiplus::RectF bgRect(cx - tw / 2, cy - th / 2, tw, th);
            g.FillRectangle(&bgBrush, bgRect);

            // 文字
            Gdiplus::PointF pt(cx, cy);
            g.DrawString(label, -1, &font, pt, &sf, &textBrush);
        }
    }
}

std::string CaptureCurrentScreen(int quality, bool grid, int maxWidth, bool grayscale) {
    // 找鼠标所在屏幕
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    int x1 = mi.rcMonitor.left;
    int y1 = mi.rcMonitor.top;
    int w = mi.rcMonitor.right - mi.rcMonitor.left;
    int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // 截屏
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HGDIOBJ oldBitmap = SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, w, h, hScreenDC, x1, y1, SRCCOPY);
    SelectObject(hMemDC, oldBitmap);

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromHBITMAP(hBitmap, NULL);

    // 缩放到 maxWidth 宽
    int outW = w, outH = h;
    if (maxWidth > 0 && outW > maxWidth) {
        outH = outH * maxWidth / outW;
        outW = maxWidth;
    }

    Gdiplus::Bitmap* outBmp = new Gdiplus::Bitmap(outW, outH, PixelFormat24bppRGB);
    {
        Gdiplus::Graphics g(outBmp);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

        if (grayscale) {
            // 灰度颜色矩阵
            Gdiplus::ColorMatrix grayMatrix = {
                0.299f, 0.299f, 0.299f, 0, 0,
                0.587f, 0.587f, 0.587f, 0, 0,
                0.114f, 0.114f, 0.114f, 0, 0,
                0,      0,      0,      1, 0,
                0,      0,      0,      0, 1
            };
            Gdiplus::ImageAttributes attrs;
            attrs.SetColorMatrix(&grayMatrix);
            Gdiplus::Rect destRect(0, 0, outW, outH);
            g.DrawImage(bmp, destRect, 0, 0, bmp->GetWidth(), bmp->GetHeight(),
                        Gdiplus::UnitPixel, &attrs);
        } else {
            g.DrawImage(bmp, 0, 0, outW, outH);
        }

        if (grid) {
            DrawGrid(g, outW, outH);
        }
    }

    // 保存
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring jpegPath = std::wstring(tempPath) + L"vimouse_screenshot.jpg";

    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);

    Gdiplus::EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG qualityValue = quality;
    encoderParams.Parameter[0].Value = &qualityValue;

    Gdiplus::Status status = outBmp->Save(jpegPath.c_str(), &jpegClsid, &encoderParams);

    delete bmp;
    delete outBmp;
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (status != Gdiplus::Ok) return "ERR failed to save JPEG";

    int pathLen = WideCharToMultiByte(CP_UTF8, 0, jpegPath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string pathUtf8(pathLen - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, jpegPath.c_str(), -1, &pathUtf8[0], pathLen, NULL, NULL);
    return "OK " + pathUtf8;
}
