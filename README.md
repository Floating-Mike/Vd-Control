# Vd.dll — Simplified Windows Virtual Desktop control for Windows 11, designed for AutoHotkey

Lightweight C++ DLL that exposes Windows' undocumented virtual-desktop COM API as plain, easy to use functions. The primary intended use is AutoHotkey automation, but it could be utilised in many other scenarios.

- Targets **Windows 11 24H2 (26100+) / 25H2 (26200+)** only.
- Active desktops are numbered (1-based, to match keyboard keys).
- Asking to jump to a non-existent desktop index creates a (single) new desktop and moves to that; it **doesn't create interstitial desktops up to the number reqeusted**.
- Animations can be switched on or off.
- Desktop creation is (artificially) restricted to a **maximum of 32**. If more have been created through other means, they can still be managed through this tool.
- Output is a **single `Vd.dll`** — no external libraries are necessary, so it should work on any up-to-date installation of Windows 11.
- Can be placed next to `.ahk` scripts and accessed using the `DllCall` functionality. See `example.ahk` for a ready-to-use segment that sets up the library and could be placed in a `#include'.

-- **Below this point this readme is machine generated** --

## API

| Export | Meaning | Returns |
|---|---|---|
| `VdGetCount()` | number of desktops | count, `-1` on error |
| `VdGetCurrent()` | current desktop (1-based) | number, `-1` on error |
| `VdGoTo(n)` | switch to desktop `n` (creates one if `n > Count`) | actual desktop used, `-1` |
| `VdGoBack()` | switch to last-active desktop (toggle) | desktop switched to, `-1` |
| `VdGoLeft()` / `VdGoRight()` | switch to neighbouring desktop (`-1` at the edge) | desktop switched to, `-1` |
| `VdRemoveCurrentDesktop()` | remove current desktop (see note below) | desktop landed on, `-1` |
| `VdMoveFocused(n)` | move focused window to `n` (creates one if beyond; stays put) | actual target, `-1` |
| `VdMoveFocusedAndGo(n)` | move focused window to `n` **and** switch there — one atomic call, at most one desktop created | actual target, `-1` |
| `VdMoveHwnd(hwnd, n)` | move explicit window handle to `n` | actual target, `-1` |
| `VdGetHwndDesktop(hwnd)` | desktop containing `hwnd` (pinned windows report current) | number, `-1` |
| `VdSetAnimation(on)` | `1`/`0`: animate all later switches; returns previous flag | previous `0`/`1` |
| `VdLastErrorCode()` / `VdLastErrorText()` | last failure diagnostics | code / UTF-8 string |

All functions return `-1` on failure; call `VdLastErrorText()` for the reason
(stale Explorer, invalid window, pinned/toolwindow, COM mismatch after a
Windows update, …). `Move` never follows — chain `VdMoveFocused(n)` +
`VdGoTo(n)` if you want move-and-follow (see `example.ahk`). Prefer the
atomic `VdMoveFocusedAndGo(n)` for that: chaining a separate move and `GoTo`
issues two ensure-calls, so an out-of-range `n` would create one desktop for
the window and a second one for the focus jump.

`VdGoBack` targets the last-active desktop, falling back to desktop 1 (always
resolved positionally, so a replacement "1" works) when that desktop is gone.
`VdRemoveCurrentDesktop` applies the same validation to its fallback choice.

Removing a desktop never closes anything: Windows moves every window on it
to the fallback desktop, which here is the last-active desktop (or desktop 1
if that *is* the one being removed). With a single desktop the call is a
silent no-op returning `1` — matching what Windows itself would do.

The DLL also holds session state: `VdSetAnimation(1)` once after
`LoadLibrary` makes every later `VdGoTo`/`VdGoBack`/`VdGoLeft`/`VdGoRight`
use the animated switch (waiting for it to finish, so rapid key repeats stay
ordered). It returns the previous flag. Default is off (instant switches).
The flag lives as long as the DLL stays loaded — for an always-running
script, that means the whole login session; a script reload resets it.

## Use in AHK v2 (brief)

```ahk
#Requires AutoHotkey v2.0
DllCall("LoadLibrary", "Str", A_ScriptDir "\Vd.dll", "Ptr")

VdErr() => StrGet(DllCall("Vd\VdLastErrorText", "Ptr"), "UTF-8")
Check(r, what) {
    if (r < 0)
        ToolTip(what " failed: " VdErr(), , , 1),
        SetTimer(() => ToolTip(, , , 1), -2500)
    return r
}
; Wrappers with error checking built in — hotkeys are one line each:
VdGoTo(n)        => Check(DllCall("Vd\VdGoTo", "Int", n, "Int"), "GoTo " n)
VdMoveFocused(n) => Check(DllCall("Vd\VdMoveFocused", "Int", n, "Int"), "Move to " n)

#1::VdGoTo(1)          ; jump to desktop 1
#+1::VdMoveFocused(1)  ; move focused window to desktop 1
```

No `GetProcAddress` dance is needed — `DllCall("Vd\Func", …)` resolves
directly. See **`example.ahk`** for the full demonstration (jump, move, move
+ follow, neighbours/back, remove, count/current inspection, out-of-range
creation, error tooltips).

## Compile & link

Sources: `src/vd.cpp`, `src/vd_com.h` (the only file with COM vtables).

**Prerequisites (VS 2026).** Either check the **“Desktop development with
C++”** workload in Visual Studio Installer > Modify, or install just the two
components this build needs (much smaller):

- **MSVC v145 – VS 2026 C++ x64/x86 build tools** (compiler *plus* its C++
  headers — `excpt.h`, `vcruntime.h` live inside `VC\Tools\MSVC\<ver>\include\`);
- **Windows 11 SDK** (`10.0.26100.0` or later — `windows.h`, `ole32.lib`).

Smoking gun for a partial install: `windows.h` fails with
`fatal error C1083: Cannot open include file: 'excpt.h'`, and `excpt.h` is
nowhere on disk. That file ships with the MSVC toolset itself, so its absence
means the C++ headers component never installed — no PATH/INCLUDE trick can
compensate. If you hit it, the Installer fix above is the whole cure.

**Build** (from an “x64 Native Tools” prompt), output `build\Vd.dll`
(intermediates — `vd.obj`, `Vd.lib`, `Vd.exp` — stay inside `build\` too):

```bat
build-msvc.bat
REM equivalent manual command:
REM cl /nologo /EHsc /W4 /O2 /LD /Isrc src\vd.cpp /Fo:build\vd.obj /link ole32.lib user32.lib /IMPLIB:build\Vd.lib /OUT:build\Vd.dll
```

Two VS 2026 gotchas that bite everyone:

- There is **no `cl` on `PATH` by default** — not even in the developer
  shells. The shells default to an **x86** target unless told otherwise, so
  with only x64 components installed nothing shows up. Always request it
  explicitly: `VsDevCmd.bat -arch=amd64` or `Launch-VsDevShell.ps1 -Arch amd64`.
- `vcvarsall.bat` still exists, but it moved with the 64-bit install:
  `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\`
  — note `Program Files`, **not** `Program Files (x86)`. Looking in the old
  x86 path or the pre-2017 `VC\bin` location is why it seems “removed”.
  In PowerShell prefer `Launch-VsDevShell.ps1` — a `.bat` cannot export
  variables into a PowerShell parent, so `vcvarsall` silently does nothing
  there. Just adding the `bin` folder to `PATH` by hand is also not enough:
  `INCLUDE`/`LIB` must be set too (a missing `excpt.h` error means exactly
  that).

**MinGW cross-compile** (Linux/macOS → 64-bit Windows, no VS needed),
output `build/Vd.dll`:

```sh
./build-mingw.sh
# equivalent manual command:
# x86_64-w64-mingw32-g++ -O2 -Wall -Wextra -shared -static-libgcc -static-libstdc++ -static \
#   -o build/Vd.dll src/vd.cpp -I src -lole32 -luser32
```

Link note: the imports are `ole32.lib` (COM) and `user32.lib`
(`GetForegroundWindow`, `IsWindow`) — `-lole32 -luser32` for MinGW; the MinGW
build static-links the C++ runtime so the result stays one file. Build x64
for 64-bit AHK.

## How it works / pitfalls

- Windows exposes no numbered API — desktops are GUIDs. The DLL enumerates
  `GetDesktops()` order and maps position ↔ 1-based number per call.
- Switching uses `SwitchDesktop`; moving uses
  `IApplicationViewCollection::GetViewForHwnd` +
  `MoveViewToDesktop` (the documented `MoveWindowToDesktop` only works
  same-process, hence the internal path).
- A fresh COM session is acquired per call, so an Explorer restart cannot
  leave stale cached interfaces behind.
- Fragility is inherent: `IVirtualDesktopManagerInternal` is undocumented and
  its IID/vtable changes between Windows releases (24H2 inserted
  `SwitchDesktopAndMoveForegroundView`, breaking all older copies). This repo
  pins `{53F5CA0B-…}` + the 24H2 order in `src/vd_com.h`.
- **After a breaking Windows update:** re-dump `twinui.pcshell.dll`
  (OleViewDotNet / WinJump dump script), update the IID/method order in
  `src/vd_com.h` only, rebuild. A mismatch surfaces as
  `IVirtualDesktopManagerInternal mismatch…` from `VdLastErrorText()`.
- Cannot-move cases (return `-1`): no foreground window, shell/toolwindows
  with no application view, pinned windows/apps, elevated windows from a
  non-elevated caller.
