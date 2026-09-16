@echo off
REM Build Vd.dll with MSVC (x64). Run from a "x64 Native Tools" prompt.
REM Single DLL output -> build\Vd.dll
setlocal
if not exist build mkdir build
where cl >nul 2>&1
if errorlevel 1 (
    echo Error: cl.exe not found on PATH. Run from a Visual Studio developer command prompt.
    exit /b 1
)
REM /Fo + /IMPLIB keep intermediates (vd.obj, Vd.lib, Vd.exp) inside build\.
cl /nologo /EHsc /W4 /O2 /LD /Isrc src\vd.cpp /Fo:build\vd.obj /link ole32.lib user32.lib /IMPLIB:build\Vd.lib /OUT:build\Vd.dll
if errorlevel 1 (
    echo Error: build failed.
    exit /b 1
)
endlocal
