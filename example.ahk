#Requires AutoHotkey v2.0
; example.ahk — full demonstration of Vd.dll possibilities.
; Copy Vd.dll next to this script, then run it (double-click works: .ahk is
; the registered AutoHotkey extension; #Requires above pins v2).
;
; Convention: desktops are 1-based (1..Count), matching the number keys.
; Out-of-range rule: asking for n > Count creates EXACTLY ONE new desktop
; and uses it. E.g. 4 exist + ask 7 -> new desktop 5 is used and returned.
;
; Hotkey choice: this demo uses Win+digit throughout. Note EVERY such
; combination is a Windows 11 default (details above each section) — fine
; here since those defaults go unused, but rebind to taste. The calls are
; one-liners whatever the hotkey.

; ================= library part (hide in an #Include) =================
SetWorkingDir(A_ScriptDir)
DllCall("LoadLibrary", "Str", A_ScriptDir "\Vd.dll", "Ptr")

VdErr() => StrGet(DllCall("Vd\VdLastErrorText", "Ptr"), "UTF-8")

Check(r, what) {
    if (r < 0)
        ToolTip(what " failed: " VdErr(), , , 1),
        SetTimer(() => ToolTip(, , , 1), -2500)
    return r
}

; Wrappers with error checking built in — the main file just calls these.
VdGoTo(n)           => Check(DllCall("Vd\VdGoTo", "Int", n, "Int"), "GoTo " n)
VdMoveFocused(n)    => Check(DllCall("Vd\VdMoveFocused", "Int", n, "Int"), "Move to " n)
VdCount()           => Check(DllCall("Vd\VdGetCount", "Int"), "GetCount")
VdCurrent()         => Check(DllCall("Vd\VdGetCurrent", "Int"), "GetCurrent")
VdMoveHwnd(hwnd, n) => Check(DllCall("Vd\VdMoveHwnd", "Ptr", hwnd, "Int", n, "Int"), "Move window")
VdHwndDesk(hwnd)    => Check(DllCall("Vd\VdGetHwndDesktop", "Ptr", hwnd, "Int"), "Window desktop")
; ================= end of library part =================

; --- 1) jump to desktop N ---
; Overrides Windows default: Win+1..9 launch / switch to taskbar-pinned apps.
#1::VdGoTo(1)
#2::VdGoTo(2)
#3::VdGoTo(3)
#4::VdGoTo(4)
#5::VdGoTo(5)
#6::VdGoTo(6)
#7::VdGoTo(7)
#8::VdGoTo(8)
#9::VdGoTo(9)

; --- 2) move focused window to desktop N (stay where you are) ---
; Overrides Windows default: Win+Shift+1.. opens a NEW INSTANCE of the
; taskbar-pinned app in that slot.
#+1::VdMoveFocused(1)
#+2::VdMoveFocused(2)
#+3::VdMoveFocused(3)
#+4::VdMoveFocused(4)

; --- 3) move + follow: move then jump (most people want this on one key) ---
; Overrides Windows default: Win+Alt+1.. opens the taskbar app's Jump List.
#!1::(VdMoveFocused(1) > 0) && VdGoTo(1)
#!2::(VdMoveFocused(2) > 0) && VdGoTo(2)

; --- 4) inspect: count / current / where-is-this-window ---
; Win+F1 overrides Windows Help; Win+F2/F3 are unassigned by default.
#F1::MsgBox("Desktops: " VdCount() "`nCurrent: " VdCurrent(), "Vd")
#F2::MsgBox("Focused hwnd " WinGetID("A") " is on desktop " VdHwndDesk(WinGetID("A")), "Vd")

; --- 5) out-of-range demo: with 4 desktops, this creates desktop 5 ---
#F3::MsgBox("Asked 7, got " VdGoTo(7), "Vd ensure-one")
