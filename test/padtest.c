// Loads ..\..\xinput1_3.dll the way BlackOps.exe does (by ordinal) and prints the pad state.
#include <windows.h>
#include <xinput.h>
#include <stdio.h>

typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE *);

int main(void)
{
    HMODULE m = LoadLibraryA("..\\..\\xinput1_3.dll");
    PFN_GetState gs;
    if (!m) { printf("load failed %lu\n", GetLastError()); return 1; }
    gs = (PFN_GetState)GetProcAddress(m, (LPCSTR)2);
    printf("ordinal 2 = %p, 100 = %p\n", (void *)gs, (void *)GetProcAddress(m, (LPCSTR)100));
    for (int t = 0; t < 3; t++) {
        for (DWORD i = 0; i < 4; i++) {
            XINPUT_STATE s;
            DWORD r = gs(i, &s);
            if (r == ERROR_SUCCESS)
                printf("pad %lu: LX=%d LY=%d RX=%d RY=%d LT=%d RT=%d buttons=%04X\n", i,
                       s.Gamepad.sThumbLX, s.Gamepad.sThumbLY, s.Gamepad.sThumbRX, s.Gamepad.sThumbRY,
                       s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger, s.Gamepad.wButtons);
            else if (t == 0)
                printf("pad %lu: not connected (%lu)\n", i, r);
        }
        Sleep(500);
    }
    return 0;
}
