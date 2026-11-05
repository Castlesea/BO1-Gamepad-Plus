@echo off
rem Builds xinput1_3.dll (32-bit) and copies it with bo1pad.ini next to BlackOps.exe
setlocal
set VCVARS=
for %%v in (18 2022 2019) do for %%e in (Community Professional Enterprise BuildTools) do (
    if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat"
)
if not exist "%VCVARS%" (echo Visual Studio C++ tools not found & exit /b 1)
call "%VCVARS%" amd64_x86 >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /W3 /LD /D_CRT_SECURE_NO_WARNINGS bo1pad.c /Fobuild\ /Febuild\xinput1_3.dll /link /DLL user32.lib || exit /b 1

rem The icon is optional: it comes from the game, so it isn't in the repository
set RES=
if exist blackops.ico (
    rc /nologo /fo build\launcher.res launcher.rc || exit /b 1
    set RES=build\launcher.res
)
cl /nologo /O2 /MT /W3 launcher.c %RES% /Fobuild\ /Fe"build\BlackOps Borderless.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator'" || exit /b 1

copy /y build\xinput1_3.dll ..\xinput1_3.dll >nul || exit /b 1
copy /y "build\BlackOps Borderless.exe" "..\BlackOps Borderless.exe" >nul || exit /b 1
if not exist ..\bo1pad.ini copy /y bo1pad.ini ..\bo1pad.ini >nul
echo Installed xinput1_3.dll
