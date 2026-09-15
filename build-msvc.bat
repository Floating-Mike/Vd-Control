@echo off
REM Build Vd.dll with MSVC (x64). Run from a "x64 Native Tools" prompt.
REM Single DLL output -> build\Vd.dll
setlocal
if not exist build mkdir build
REM /Fo + /IMPLIB keep intermediates (vd.obj, Vd.lib, Vd.exp) inside build\.
cl /nologo /EHsc /W4 /O2 /LD /Isrc src\vd.cpp /Fo:build\vd.obj /link ole32.lib user32.lib /IMPLIB:build\Vd.lib /OUT:build\Vd.dll
endlocal
