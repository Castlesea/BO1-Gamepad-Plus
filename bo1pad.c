// bo1pad - controller support fix for Call of Duty: Black Ops (BlackOps.exe, SP/Zombies)
//
// Built as xinput1_3.dll and dropped next to BlackOps.exe. It forwards everything to the
// real system XInput and adds:
//   * AimMode=mouse: right stick is turned into mouse look inside the game's own
//     GetCursorPos/SetCursorPos calls, sampled at the moment the game reads the mouse.
//     That skips the console aim pipeline (turn-rate acceleration, input graph, slowdown)
//     which is what makes native gamepad aim feel delayed.
//   * Radial deadzones for both sticks.
//   * [Dvars] overrides written straight into the dvar pool, so cheat-protected
//     aim_*/gpad_* dvars can be changed.
// bo1pad.ini is reloaded automatically when it changes, so tuning works while playing.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <timeapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

#pragma comment(linker, "/EXPORT:XInputGetState=_Proxy_XInputGetState@8,@2")
#pragma comment(linker, "/EXPORT:XInputSetState=_Proxy_XInputSetState@8,@3")
#pragma comment(linker, "/EXPORT:XInputGetCapabilities=_Proxy_XInputGetCapabilities@12,@4")
#pragma comment(linker, "/EXPORT:XInputEnable=_Proxy_XInputEnable@4,@5")
#pragma comment(linker, "/EXPORT:XInputGetDSoundAudioDeviceGuids=_Proxy_XInputGetDSoundAudioDeviceGuids@12,@6")
#pragma comment(linker, "/EXPORT:XInputGetBatteryInformation=_Proxy_XInputGetBatteryInformation@12,@7")
#pragma comment(linker, "/EXPORT:XInputGetKeystroke=_Proxy_XInputGetKeystroke@12,@8")
#pragma comment(linker, "/EXPORT:XInputGetStateEx=_Proxy_XInputGetStateEx@8,@100,NONAME")
#pragma comment(linker, "/EXPORT:XInputWaitForGuideButton=_Proxy_XInputWaitForGuideButton@12,@101,NONAME")
#pragma comment(linker, "/EXPORT:XInputCancelGuideButtonWait=_Proxy_XInputCancelGuideButtonWait@4,@102,NONAME")
#pragma comment(linker, "/EXPORT:XInputPowerOffController=_Proxy_XInputPowerOffController@4,@103,NONAME")

// ---------------------------------------------------------------------------------------
// Real XInput

typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *PFN_SetState)(DWORD, XINPUT_VIBRATION *);
typedef DWORD (WINAPI *PFN_GetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES *);
typedef void  (WINAPI *PFN_Enable)(BOOL);
typedef DWORD (WINAPI *PFN_GetDSound)(DWORD, GUID *, GUID *);
typedef DWORD (WINAPI *PFN_GetBattery)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);
typedef DWORD (WINAPI *PFN_GetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE);
typedef DWORD (WINAPI *PFN_WaitGuide)(DWORD, DWORD, void *);
typedef DWORD (WINAPI *PFN_DwordOnly)(DWORD);

static HMODULE g_real;
static PFN_GetState g_GetState, g_GetStateEx;
static PFN_SetState g_SetState;
static PFN_GetCaps g_GetCaps;
static PFN_Enable g_Enable;
static PFN_GetDSound g_GetDSound;
static PFN_GetBattery g_GetBattery;
static PFN_GetKeystroke g_GetKeystroke;
static PFN_WaitGuide g_WaitGuide;
static PFN_DwordOnly g_CancelGuide, g_PowerOff;

static INIT_ONCE g_realOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK LoadReal(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    static const wchar_t *names[] = { L"\\XInput1_3.dll", L"\\XInput1_4.dll", L"\\XInput9_1_0.dll" };
    wchar_t path[MAX_PATH];
    UINT len = GetSystemDirectoryW(path, MAX_PATH);
    (void)once; (void)param; (void)ctx;
    for (int i = 0; i < 3 && !g_real && len; i++) {
        wchar_t full[MAX_PATH];
        lstrcpyW(full, path);
        lstrcatW(full, names[i]);
        g_real = LoadLibraryW(full);
    }
    if (!g_real) return TRUE;
    g_GetState     = (PFN_GetState)GetProcAddress(g_real, "XInputGetState");
    g_SetState     = (PFN_SetState)GetProcAddress(g_real, "XInputSetState");
    g_GetCaps      = (PFN_GetCaps)GetProcAddress(g_real, "XInputGetCapabilities");
    g_Enable       = (PFN_Enable)GetProcAddress(g_real, "XInputEnable");
    g_GetDSound    = (PFN_GetDSound)GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids");
    g_GetBattery   = (PFN_GetBattery)GetProcAddress(g_real, "XInputGetBatteryInformation");
    g_GetKeystroke = (PFN_GetKeystroke)GetProcAddress(g_real, "XInputGetKeystroke");
    g_GetStateEx   = (PFN_GetState)GetProcAddress(g_real, (LPCSTR)100);
    g_WaitGuide    = (PFN_WaitGuide)GetProcAddress(g_real, (LPCSTR)101);
    g_CancelGuide  = (PFN_DwordOnly)GetProcAddress(g_real, (LPCSTR)102);
    g_PowerOff     = (PFN_DwordOnly)GetProcAddress(g_real, (LPCSTR)103);
    return TRUE;
}

static void EnsureReal(void) { InitOnceExecuteOnce(&g_realOnce, LoadReal, NULL, NULL); }

// ---------------------------------------------------------------------------------------
// Config

typedef struct {
    int   aimMouse;          // 1 = right stick -> mouse look, 0 = native gamepad aim
    int   log;
    int   dpadArrows;        // 1 = D-pad sends the arrow keys, which is what menus navigate with
    float yawSpeed, pitchSpeed;      // degrees per second at full deflection
    float rDeadzone, rOuter, curve;
    float adsMult, adsTrigger;
    float boostMult, boostThreshold, boostRamp;
    int   invertPitch;
    float lDeadzone, lAntiDeadzone;
} Config;

#define MAX_DVARS 64
typedef struct { char name[64]; char value[64]; void *dvar; } DvarOverride;

static Config g_cfg;
static DvarOverride g_dvars[MAX_DVARS];
static int g_dvarCount;
static CRITICAL_SECTION g_lock;
static char g_dir[MAX_PATH], g_iniPath[MAX_PATH], g_logPath[MAX_PATH];
static FILETIME g_iniTime;

static void Log(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    if (!g_cfg.log || fopen_s(&f, g_logPath, "a") || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static float IniFloat(const char *sec, const char *key, float def)
{
    char buf[64], defs[64];
    sprintf_s(defs, sizeof defs, "%g", def);
    GetPrivateProfileStringA(sec, key, defs, buf, sizeof buf, g_iniPath);
    return (float)atof(buf);
}

static void LoadConfig(void)
{
    Config c;
    char mode[32], section[8192];
    GetPrivateProfileStringA("General", "AimMode", "mouse", mode, sizeof mode, g_iniPath);
    c.aimMouse       = _stricmp(mode, "native") != 0;
    c.log            = GetPrivateProfileIntA("General", "Log", 1, g_iniPath);
    c.dpadArrows     = GetPrivateProfileIntA("General", "DpadMenuArrows", 1, g_iniPath);
    c.yawSpeed       = IniFloat("MouseAim", "YawSpeed", 300.0f);
    c.pitchSpeed     = IniFloat("MouseAim", "PitchSpeed", 200.0f);
    c.rDeadzone      = IniFloat("MouseAim", "Deadzone", 0.08f);
    c.rOuter         = IniFloat("MouseAim", "OuterDeadzone", 0.97f);
    c.curve          = IniFloat("MouseAim", "Curve", 1.8f);
    c.adsMult        = IniFloat("MouseAim", "AdsMultiplier", 1.0f);
    c.adsTrigger     = IniFloat("MouseAim", "AdsTriggerThreshold", 0.3f);
    c.boostMult      = IniFloat("MouseAim", "BoostMultiplier", 1.6f);
    c.boostThreshold = IniFloat("MouseAim", "BoostThreshold", 0.95f);
    c.boostRamp      = IniFloat("MouseAim", "BoostRampTime", 0.25f);
    c.invertPitch    = GetPrivateProfileIntA("MouseAim", "InvertPitch", 0, g_iniPath);
    c.lDeadzone      = IniFloat("LeftStick", "Deadzone", 0.10f);
    c.lAntiDeadzone  = IniFloat("LeftStick", "AntiDeadzone", 0.0f);
    if (c.rOuter <= c.rDeadzone) c.rOuter = c.rDeadzone + 0.01f;
    if (c.curve < 0.1f) c.curve = 0.1f;

    DWORD n = GetPrivateProfileSectionA("Dvars", section, sizeof section, g_iniPath);
    DvarOverride dv[MAX_DVARS];
    int count = 0;
    for (char *p = section; n && *p && count < MAX_DVARS; p += strlen(p) + 1) {
        char *eq = strchr(p, '=');
        if (!eq || *p == ';' || *p == '#') continue;
        *eq = 0;
        strncpy_s(dv[count].name, sizeof dv[count].name, p, _TRUNCATE);
        strncpy_s(dv[count].value, sizeof dv[count].value, eq + 1, _TRUNCATE);
        dv[count].dvar = NULL;
        *eq = '=';
        count++;
    }

    EnterCriticalSection(&g_lock);
    g_cfg = c;
    memcpy(g_dvars, dv, sizeof(DvarOverride) * count);
    g_dvarCount = count;
    LeaveCriticalSection(&g_lock);
    Log("config loaded: AimMode=%s Yaw=%.0f Pitch=%.0f Curve=%.2f RDZ=%.2f LDZ=%.2f dvars=%d",
        c.aimMouse ? "mouse" : "native", c.yawSpeed, c.pitchSpeed, c.curve, c.rDeadzone,
        c.lDeadzone, count);
}

static int IniChanged(void)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &a)) return 0;
    if (CompareFileTime(&a.ftLastWriteTime, &g_iniTime) == 0) return 0;
    g_iniTime = a.ftLastWriteTime;
    return 1;
}

// ---------------------------------------------------------------------------------------
// Dvars (layout taken from BlackOps.exe Dvar_RegisterNew)
//   name +0x00, flags +0x0C, type +0x10, current +0x18, latched +0x28; stride 0x70

#define DVAR_COUNT_ADDR   0x0261CBD4
#define DVAR_PTRS_ADDR    0x0261CBE8
#define DVAR_POOL_ADDR    0x02621BF0
#define DVAR_STRIDE       0x70
#define DVAR_MAX          0x1000

enum { DV_BOOL, DV_FLOAT, DV_VEC2, DV_VEC3, DV_VEC4, DV_INT, DV_ENUM, DV_STRING, DV_COLOR };

static int g_dvarsOk;

static void CheckDvarLayout(void)
{
    // add esi, 0x2621bf0 inside Dvar_RegisterNew
    static const unsigned char sig[] = { 0x6B, 0xF6, 0x70, 0x81, 0xC6, 0xF0, 0x1B, 0x62, 0x02 };
    __try {
        g_dvarsOk = memcmp((void *)0x00862B7E, sig, sizeof sig) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_dvarsOk = 0;
    }
    Log(g_dvarsOk ? "dvar layout recognised" : "unknown BlackOps.exe build: dvar overrides disabled");
}

static unsigned char *FindDvar(const char *name)
{
    if (!g_dvarsOk) return NULL;
    __try {
        int count = *(int *)DVAR_COUNT_ADDR;
        unsigned char **ptrs = (unsigned char **)DVAR_PTRS_ADDR;
        if (count <= 0 || count > DVAR_MAX) return NULL;
        for (int i = 0; i < count; i++) {
            unsigned char *d = ptrs[i];
            const char *n;
            if ((ULONG_PTR)d < DVAR_POOL_ADDR || (ULONG_PTR)d >= DVAR_POOL_ADDR + DVAR_STRIDE * DVAR_MAX)
                continue;
            n = *(const char **)d;
            if (n && _stricmp(n, name) == 0) return d;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return NULL;
}

static float DvarFloat(unsigned char *d, float def)
{
    if (!d) return def;
    switch (*(int *)(d + 0x10)) {
    case DV_FLOAT: return *(float *)(d + 0x18);
    case DV_INT: case DV_ENUM: return (float)*(int *)(d + 0x18);
    case DV_BOOL: return *(d + 0x18) ? 1.0f : 0.0f;
    }
    return def;
}

static void ApplyDvars(void)
{
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_dvarCount; i++) {
        DvarOverride *o = &g_dvars[i];
        unsigned char *d = o->dvar ? (unsigned char *)o->dvar : FindDvar(o->name);
        if (!d) continue;
        if (!o->dvar) {
            o->dvar = d;
            Log("dvar %s found (type %d, flags 0x%X) -> %s", o->name, *(int *)(d + 0x10),
                *(unsigned *)(d + 0x0C), o->value);
        }
        switch (*(int *)(d + 0x10)) {
        case DV_BOOL: {
            unsigned char v = (unsigned char)(atoi(o->value) != 0);
            if (d[0x18] != v) { d[0x18] = v; d[0x28] = v; }
            break;
        }
        case DV_FLOAT: {
            float v = (float)atof(o->value);
            if (*(float *)(d + 0x18) != v) { *(float *)(d + 0x18) = v; *(float *)(d + 0x28) = v; }
            break;
        }
        case DV_INT: case DV_ENUM: {
            int v = atoi(o->value);
            if (*(int *)(d + 0x18) != v) { *(int *)(d + 0x18) = v; *(int *)(d + 0x28) = v; }
            break;
        }
        }
    }
    LeaveCriticalSection(&g_lock);
}

// The game's own button table, { XInput mask | 0x10000000, key code } per button.
// Pointing the D-pad entries at the arrow keys makes the game send those keys itself.
#define PAD_KEY_TABLE 0x00B7F0C8
#define PAD_KEY_COUNT 16

static void ApplyDpadKeys(void)
{
    static const struct { DWORD mask, dpadKey, arrowKey; } map[] = {
        { 0x10000001, 0x14, 0x9A },   // up
        { 0x10000002, 0x15, 0x9B },   // down
        { 0x10000004, 0x16, 0x9C },   // left
        { 0x10000008, 0x17, 0x9D },   // right
    };
    if (!g_dvarsOk) return;
    __try {
        DWORD *table = (DWORD *)PAD_KEY_TABLE;
        for (int i = 0; i < PAD_KEY_COUNT; i++) {
            for (int m = 0; m < 4; m++) {
                DWORD want = g_cfg.dpadArrows ? map[m].arrowKey : map[m].dpadKey;
                DWORD old;
                if (table[i * 2] != map[m].mask || table[i * 2 + 1] == want) continue;
                if (table[i * 2 + 1] != map[m].dpadKey && table[i * 2 + 1] != map[m].arrowKey) continue;
                VirtualProtect(&table[i * 2 + 1], sizeof(DWORD), PAGE_READWRITE, &old);
                table[i * 2 + 1] = want;
                VirtualProtect(&table[i * 2 + 1], sizeof(DWORD), old, &old);
                Log("D-pad mask %08X -> key 0x%X", map[m].mask, want);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("could not change the D-pad keys");
    }
}

// ---------------------------------------------------------------------------------------
// Stick processing

static void RadialDeadzone(SHORT *px, SHORT *py, float dz, float outer, float anti)
{
    float x = *px / 32767.0f, y = *py / 32767.0f;
    float m = sqrtf(x * x + y * y), out;
    if (m <= dz) { *px = *py = 0; return; }
    out = (m - dz) / (outer - dz);
    if (out > 1.0f) out = 1.0f;
    out = anti + (1.0f - anti) * out;
    x = x / m * out;
    y = y / m * out;
    *px = (SHORT)(x * 32767.0f);
    *py = (SHORT)(y * 32767.0f);
}

// Activity counters, written to bo1pad.log every 5 seconds while something is happening
static volatile LONG g_statGetState, g_statGetCursor, g_statSetCursor, g_statDisarm, g_statSent;

// Latest pad state from the poll thread
static XINPUT_STATE g_pad;
static int g_padSlot = -1;

// Mouse look injection state (game thread only, except g_pad which is read under lock)
static double g_pendX, g_pendY;
static LONG g_retX, g_retY;           // offset handed to the game by the last GetCursorPos
static int g_armed;
static double g_offerTime, g_lastTick, g_boostTime, g_disarmTime;
static volatile LONG g_mouseLookActive;
static LARGE_INTEGER g_qpf;

static double Now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)g_qpf.QuadPart;
}

static unsigned char *g_dvSens, *g_dvYaw, *g_dvPitch, *g_dvInvert;

static void AdvanceAim(double now)
{
    XINPUT_STATE s;
    Config c;
    double dt = now - g_lastTick;
    g_lastTick = now;
    if (dt <= 0.0) return;
    if (dt > 0.05) dt = 0.05;

    EnterCriticalSection(&g_lock);
    s = g_pad;
    c = g_cfg;
    LeaveCriticalSection(&g_lock);
    if (g_padSlot < 0) return;

    float x = s.Gamepad.sThumbRX / 32767.0f, y = s.Gamepad.sThumbRY / 32767.0f;
    float m = sqrtf(x * x + y * y);
    if (m <= c.rDeadzone) { g_boostTime = 0; return; }
    float n = (m - c.rDeadzone) / (c.rOuter - c.rDeadzone);
    if (n > 1.0f) n = 1.0f;
    float shaped = powf(n, c.curve);
    float dx = x / m * shaped, dy = y / m * shaped;

    float boost = 1.0f;
    if (n >= c.boostThreshold && c.boostMult > 1.0f) {
        g_boostTime += dt;
        boost = 1.0f + (c.boostMult - 1.0f) *
                (c.boostRamp > 0.0f ? (float)min(1.0, g_boostTime / c.boostRamp) : 1.0f);
    } else {
        g_boostTime = 0;
    }

    float ads = s.Gamepad.bLeftTrigger / 255.0f >= c.adsTrigger ? c.adsMult : 1.0f;

    if (!g_dvSens) g_dvSens = FindDvar("sensitivity");
    if (!g_dvYaw) g_dvYaw = FindDvar("m_yaw");
    if (!g_dvPitch) g_dvPitch = FindDvar("m_pitch");
    if (!g_dvInvert) g_dvInvert = FindDvar("input_invertPitch");
    float sens = DvarFloat(g_dvSens, 5.0f);
    float myaw = fabsf(DvarFloat(g_dvYaw, 0.022f));
    float mpitch = fabsf(DvarFloat(g_dvPitch, 0.022f));
    if (sens < 0.01f) sens = 0.01f;
    if (myaw < 0.0001f) myaw = 0.022f;
    if (mpitch < 0.0001f) mpitch = 0.022f;
    int invert = (DvarFloat(g_dvInvert, 0.0f) != 0.0f) ^ (c.invertPitch != 0);

    // degrees/sec -> mouse counts/sec via the game's own mouse scaling
    g_pendX += dx * c.yawSpeed * boost * ads / (sens * myaw) * dt;
    g_pendY += (invert ? dy : -dy) * c.pitchSpeed * ads / (sens * mpitch) * dt;
}

typedef BOOL (WINAPI *PFN_GetCursorPos)(LPPOINT);
typedef BOOL (WINAPI *PFN_SetCursorPos)(int, int);
static PFN_GetCursorPos g_origGetCursorPos;
static PFN_SetCursorPos g_origSetCursorPos;

static BOOL WINAPI Hook_GetCursorPos(LPPOINT p)
{
    BOOL r = g_origGetCursorPos(p);
    double now;
    InterlockedIncrement(&g_statGetCursor);
    if (!r || !p || !g_cfg.aimMouse) return r;

    now = Now();
    // Offered motion that the game did not consume: it's not in mouse look (menus etc.)
    if (g_armed && (g_retX || g_retY) && now - g_offerTime > 0.3) {
        g_armed = 0;
        g_pendX = g_pendY = 0;
        g_retX = g_retY = 0;
        g_disarmTime = now;
        InterlockedExchange(&g_mouseLookActive, 0);
        InterlockedIncrement(&g_statDisarm);
    }
    // While disarmed, probe once a second in case the game went back to mouse look
    // without recentering the cursor yet.
    if (!g_armed && now - g_disarmTime > 1.0) {
        g_armed = 1;
        g_lastTick = now;
    }
    if (!g_armed) return r;

    AdvanceAim(now);
    LONG rx = (LONG)g_pendX, ry = (LONG)g_pendY;
    if ((rx || ry) && !g_retX && !g_retY) g_offerTime = now;
    g_retX = rx;
    g_retY = ry;
    p->x += rx;
    p->y += ry;
    return r;
}

static BOOL WINAPI Hook_SetCursorPos(int x, int y)
{
    InterlockedIncrement(&g_statSetCursor);
    if (g_cfg.aimMouse) {
        double now = Now();
        if (!g_armed) g_lastTick = now;
        InterlockedExchangeAdd(&g_statSent, labs(g_retX) + labs(g_retY));
        g_pendX -= g_retX;
        g_pendY -= g_retY;
        g_retX = g_retY = 0;
        g_armed = 1;
        InterlockedExchange(&g_mouseLookActive, 1);
    }
    return g_origSetCursorPos(x, y);
}

static void HookIAT(void)
{
    HMODULE exe = GetModuleHandleW(NULL);
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)exe;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE *)exe + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE *)exe + dir.VirtualAddress);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    FARPROC realGet = GetProcAddress(user32, "GetCursorPos");
    FARPROC realSet = GetProcAddress(user32, "SetCursorPos");
    if (!dir.VirtualAddress) return;

    for (; imp->Name; imp++) {
        if (_stricmp((char *)exe + imp->Name, "user32.dll") != 0) continue;
        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE *)exe + imp->FirstThunk);
        PIMAGE_THUNK_DATA orig = imp->OriginalFirstThunk
            ? (PIMAGE_THUNK_DATA)((BYTE *)exe + imp->OriginalFirstThunk) : NULL;
        for (; thunk->u1.Function; thunk++, orig = orig ? orig + 1 : NULL) {
            const char *name = NULL;
            if (orig && !IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                name = ((PIMAGE_IMPORT_BY_NAME)((BYTE *)exe + orig->u1.AddressOfData))->Name;
            void *target = NULL;
            if ((name && !strcmp(name, "GetCursorPos")) || (FARPROC)thunk->u1.Function == realGet) {
                if ((void *)thunk->u1.Function != (void *)Hook_GetCursorPos) {
                    g_origGetCursorPos = (PFN_GetCursorPos)thunk->u1.Function;
                    target = (void *)Hook_GetCursorPos;
                }
            } else if ((name && !strcmp(name, "SetCursorPos")) || (FARPROC)thunk->u1.Function == realSet) {
                if ((void *)thunk->u1.Function != (void *)Hook_SetCursorPos) {
                    g_origSetCursorPos = (PFN_SetCursorPos)thunk->u1.Function;
                    target = (void *)Hook_SetCursorPos;
                }
            }
            if (target) {
                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
                thunk->u1.Function = (ULONG_PTR)target;
                VirtualProtect(&thunk->u1.Function, sizeof(void *), old, &old);
                Log("hooked %s", target == (void *)Hook_GetCursorPos ? "GetCursorPos" : "SetCursorPos");
            }
        }
    }
}

// ---------------------------------------------------------------------------------------
// Poll thread: 1 kHz pad sampling, config reload, dvar overrides

static DWORD WINAPI PollThread(void *arg)
{
    double lastSlow = 0, lastScan = 0, lastStats = 0;
    int lastLoggedSlot = -2;
    WORD seenButtons = 0;
    BYTE seenLT = 0, seenRT = 0;
    SHORT seenRX = 0, seenRY = 0, seenLX = 0, seenLY = 0;
    (void)arg;
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    EnsureReal();
    for (;;) {
        double now = Now();
        XINPUT_STATE s;
        int slot = g_padSlot;

        if (g_GetState) {
            if (slot >= 0 && g_GetState(slot, &s) != ERROR_SUCCESS) slot = -1;
            // Scanning empty slots is slow in XInput, so only do it once a second
            if (slot < 0 && now - lastScan > 1.0) {
                lastScan = now;
                for (int i = 0; i < XUSER_MAX_COUNT; i++)
                    if (g_GetState(i, &s) == ERROR_SUCCESS) { slot = i; break; }
            }
            EnterCriticalSection(&g_lock);
            if (slot >= 0) g_pad = s; else memset(&g_pad, 0, sizeof g_pad);
            g_padSlot = slot;
            LeaveCriticalSection(&g_lock);
            if (slot != lastLoggedSlot) {
                Log(slot >= 0 ? "controller on slot %d" : "no controller", slot);
                lastLoggedSlot = slot;
            }
            if (slot >= 0) {
                seenButtons |= s.Gamepad.wButtons;
                if (s.Gamepad.bLeftTrigger > seenLT) seenLT = s.Gamepad.bLeftTrigger;
                if (s.Gamepad.bRightTrigger > seenRT) seenRT = s.Gamepad.bRightTrigger;
                if (abs(s.Gamepad.sThumbRX) > abs(seenRX)) seenRX = s.Gamepad.sThumbRX;
                if (abs(s.Gamepad.sThumbRY) > abs(seenRY)) seenRY = s.Gamepad.sThumbRY;
                if (abs(s.Gamepad.sThumbLX) > abs(seenLX)) seenLX = s.Gamepad.sThumbLX;
                if (abs(s.Gamepad.sThumbLY) > abs(seenLY)) seenLY = s.Gamepad.sThumbLY;
            }
        }

        if (now - lastStats > 5.0) {
            lastStats = now;
            Log("stats 5s: game GetState=%ld GetCursorPos=%ld SetCursorPos=%ld aimSent=%ld disarms=%ld "
                "armed=%d look=%ld | pad buttons=%04X LT=%d RT=%d L=%d,%d R=%d,%d",
                InterlockedExchange(&g_statGetState, 0), InterlockedExchange(&g_statGetCursor, 0),
                InterlockedExchange(&g_statSetCursor, 0), InterlockedExchange(&g_statSent, 0),
                InterlockedExchange(&g_statDisarm, 0), g_armed, g_mouseLookActive, seenButtons, seenLT,
                seenRT, seenLX, seenLY, seenRX, seenRY);
            seenButtons = 0;
            seenLT = seenRT = 0;
            seenRX = seenRY = seenLX = seenLY = 0;
        }

        if (now - lastSlow > 0.5) {
            lastSlow = now;
            if (IniChanged()) LoadConfig();
            HookIAT();
            ApplyDvars();
            ApplyDpadKeys();
        }
        Sleep(1);
    }
}

// ---------------------------------------------------------------------------------------
// Exports

static void ProcessForGame(XINPUT_STATE *s)
{
    Config c = g_cfg;
    RadialDeadzone(&s->Gamepad.sThumbLX, &s->Gamepad.sThumbLY, c.lDeadzone, 0.98f, c.lAntiDeadzone);
    if (c.aimMouse && g_mouseLookActive)
        s->Gamepad.sThumbRX = s->Gamepad.sThumbRY = 0;
    else
        RadialDeadzone(&s->Gamepad.sThumbRX, &s->Gamepad.sThumbRY, c.rDeadzone, c.rOuter, 0.0f);
}

DWORD WINAPI Proxy_XInputGetState(DWORD i, XINPUT_STATE *s)
{
    DWORD r;
    EnsureReal();
    InterlockedIncrement(&g_statGetState);
    if (!g_GetState) return ERROR_DEVICE_NOT_CONNECTED;
    r = g_GetState(i, s);
    if (r == ERROR_SUCCESS && s) ProcessForGame(s);
    return r;
}

DWORD WINAPI Proxy_XInputGetStateEx(DWORD i, XINPUT_STATE *s)
{
    DWORD r;
    EnsureReal();
    if (!g_GetStateEx) return Proxy_XInputGetState(i, s);
    r = g_GetStateEx(i, s);
    if (r == ERROR_SUCCESS && s) ProcessForGame(s);
    return r;
}

DWORD WINAPI Proxy_XInputSetState(DWORD i, XINPUT_VIBRATION *v)
{
    EnsureReal();
    return g_SetState ? g_SetState(i, v) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputGetCapabilities(DWORD i, DWORD f, XINPUT_CAPABILITIES *c)
{
    EnsureReal();
    return g_GetCaps ? g_GetCaps(i, f, c) : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI Proxy_XInputEnable(BOOL e)
{
    EnsureReal();
    if (g_Enable) g_Enable(e);
}

DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD i, GUID *a, GUID *b)
{
    EnsureReal();
    return g_GetDSound ? g_GetDSound(i, a, b) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputGetBatteryInformation(DWORD i, BYTE t, XINPUT_BATTERY_INFORMATION *b)
{
    EnsureReal();
    return g_GetBattery ? g_GetBattery(i, t, b) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputGetKeystroke(DWORD i, DWORD r, PXINPUT_KEYSTROKE k)
{
    EnsureReal();
    return g_GetKeystroke ? g_GetKeystroke(i, r, k) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputWaitForGuideButton(DWORD i, DWORD f, void *o)
{
    EnsureReal();
    return g_WaitGuide ? g_WaitGuide(i, f, o) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputCancelGuideButtonWait(DWORD i)
{
    EnsureReal();
    return g_CancelGuide ? g_CancelGuide(i) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI Proxy_XInputPowerOffController(DWORD i)
{
    EnsureReal();
    return g_PowerOff ? g_PowerOff(i) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char *slash;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        QueryPerformanceFrequency(&g_qpf);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
        sprintf_s(g_iniPath, sizeof g_iniPath, "%sbo1pad.ini", g_dir);
        sprintf_s(g_logPath, sizeof g_logPath, "%sbo1pad.log", g_dir);
        DeleteFileA(g_logPath);
        IniChanged();
        LoadConfig();
        Log("bo1pad loaded");
        CheckDvarLayout();
        HookIAT();
        CreateThread(NULL, 0, PollThread, NULL, 0, NULL);
    }
    return TRUE;
}
