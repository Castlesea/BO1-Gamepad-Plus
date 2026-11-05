// BlackOps Borderless.exe - starts BlackOps.exe windowed at the monitor's resolution and
// strips the window frame so it covers the whole screen (borderless fullscreen).
// Stays in the background until the game exits, re-applying the style if the game
// recreates its window (vid_restart, changing video options).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

static wchar_t g_logPath[MAX_PATH];

static void Log(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    SYSTEMTIME t;
    if (_wfopen_s(&f, g_logPath, L"a") || !f) return;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// r_fullscreen from the command line doesn't survive: config.cfg still comes back with
// r_fullscreen 1. So the windowed settings are written into the config before starting.
static void WriteWindowedConfig(const wchar_t *dir, int w, int h)
{
    wchar_t path[MAX_PATH];
    FILE *f;
    char *in, *out, *p;
    long size;
    size_t used = 0;
    int found = 0;
    char lines[4][64];

    swprintf_s(path, MAX_PATH, L"%s\\players\\config.cfg", dir);
    if (_wfopen_s(&f, path, L"rb") || !f) { Log("config.cfg not found"); return; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    in = (char *)malloc(size + 1);
    out = (char *)malloc(size + 256);
    size = (long)fread(in, 1, size, f);
    in[size] = 0;
    fclose(f);

    sprintf_s(lines[0], 64, "seta r_fullscreen \"0\"");
    sprintf_s(lines[1], 64, "seta r_mode \"%dx%d\"", w, h);
    sprintf_s(lines[2], 64, "seta vid_xpos \"0\"");
    sprintf_s(lines[3], 64, "seta vid_ypos \"0\"");

    for (p = in; *p; ) {
        char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) + 1 : strlen(p);
        const char *repl = NULL;
        if (!strncmp(p, "seta r_fullscreen ", 18)) repl = lines[0];
        else if (!strncmp(p, "seta r_mode ", 12)) repl = lines[1];
        else if (!strncmp(p, "seta vid_xpos ", 14)) repl = lines[2];
        else if (!strncmp(p, "seta vid_ypos ", 14)) repl = lines[3];
        if (repl) {
            used += sprintf_s(out + used, size + 256 - used, "%s%s", repl, len > 1 && p[len - 2] == '\r' ? "\r\n" : "\n");
            found++;
        } else {
            memcpy(out + used, p, len);
            used += len;
        }
        p += len;
    }

    if (found && !_wfopen_s(&f, path, L"wb") && f) {
        fwrite(out, 1, used, f);
        fclose(f);
    }
    Log("config.cfg: %d lines set to windowed %dx%d", found, w, h);
    free(in);
    free(out);
}

typedef struct { DWORD pid; int minW, minH; HWND hwnd; } FindCtx;

// The render window is the one whose client area is about the size of r_mode;
// smaller windows (splash, message boxes) are left alone.
static BOOL CALLBACK FindGameWindow(HWND h, LPARAM lp)
{
    FindCtx *c = (FindCtx *)lp;
    DWORD pid;
    RECT r;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    GetClientRect(h, &r);
    if (r.right - r.left < c->minW || r.bottom - r.top < c->minH) return TRUE;
    c->hwnd = h;
    return FALSE;
}

static void MakeBorderless(HWND h)
{
    MONITORINFO mi = { sizeof mi };
    RECT r;
    LONG style = GetWindowLongW(h, GWL_STYLE);
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    LONG newStyle = style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME);
    LONG newEx = ex & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);

    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    GetWindowRect(h, &r);
    if (newStyle == style && newEx == ex && EqualRect(&r, &mi.rcMonitor)) return;

    Log("window %p style %08X -> %08X, rect %d,%d,%d,%d -> monitor %d,%d,%d,%d", (void *)h,
        style, newStyle, r.left, r.top, r.right, r.bottom, mi.rcMonitor.left, mi.rcMonitor.top,
        mi.rcMonitor.right, mi.rcMonitor.bottom);
    SetWindowLongW(h, GWL_STYLE, newStyle);
    SetWindowLongW(h, GWL_EXSTYLE, newEx);
    SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show)
{
    wchar_t dir[MAX_PATH], exe[MAX_PATH], cmd[2048];
    wchar_t *slash;
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    int w, h;
    (void)inst; (void)prev; (void)show;

    SetProcessDPIAware();
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);

    GetModuleFileNameW(NULL, dir, MAX_PATH);
    slash = wcsrchr(dir, L'\\');
    if (slash) slash[0] = 0;
    swprintf_s(exe, MAX_PATH, L"%s\\BlackOps.exe", dir);
    swprintf_s(g_logPath, MAX_PATH, L"%s\\borderless.log", dir);
    DeleteFileW(g_logPath);
    WriteWindowedConfig(dir, w, h);
    swprintf_s(cmd, 2048,
               L"\"%s\" +set r_fullscreen 0 +set r_mode %dx%d +set vid_xpos 0 +set vid_ypos 0 %s",
               exe, w, h, args ? args : L"");

    // The launcher is built with requireAdministrator: BlackOps.exe is set to run as admin,
    // and a non-elevated process could neither start it nor restyle its window.
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"Could not start:\n%s\n\nError %lu.", exe, GetLastError());
        MessageBoxW(NULL, msg, L"BlackOps Borderless", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);

    while (WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT) {
        FindCtx c = { pi.dwProcessId, w * 3 / 4, h * 3 / 4, NULL };
        EnumWindows(FindGameWindow, (LPARAM)&c);
        if (c.hwnd) MakeBorderless(c.hwnd);
    }
    CloseHandle(pi.hProcess);
    return 0;
}
