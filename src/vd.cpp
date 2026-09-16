// vd.cpp — single-DLL virtual-desktop helper for Windows 11 25H2+.
//
// 1-based numbering end to end: desktop numbers are 1..Count.
// Out-of-range rule: requesting n > Count creates EXACTLY ONE new desktop
// and operates on it (e.g. 4 exist + ask 7 -> new desktop 5 is used).
// Safety cap: creation is refused once 32 desktops exist (max total is
// exactly 32). Using an already-existing high number still works; only a
// call that would create desktop 33+ fails with -1 (see VdEnsure).
//
// Exports (all __stdcall so AHK DllCall needs no "Cdecl" suffix):
//   int VdGetCount()                 -> count, or -1 on error
//   int VdGetCurrent()               -> 1-based current desktop, or -1
//   int VdGoTo(int n)                -> actual 1-based desktop switched to, or -1
//   int VdGoBack()                   -> 1-based last-active desktop switched to, or -1
//   int VdGoLeft() / VdGoRight()     -> 1-based neighbour switched to, or -1
//   int VdRemoveCurrentDesktop()     -> 1-based desktop landed on after removal, or -1
//                                      (single desktop: silent no-op, returns 1;
//                                       windows on the removed desktop move to
//                                       the fallback = last-active desktop)
//   int VdMoveFocused(int n)         -> actual 1-based target, window stays or -1
//   int VdMoveHwnd(HWND hwnd, int n) -> actual 1-based target, or -1
//   int VdMoveFocusedAndGo(int n)    -> actual 1-based target; window moved AND
//                                      focus switched in one call (single
//                                      desktop created if n out of range,
//                                      up to 32 total)
//   int VdGetHwndDesktop(HWND hwnd)  -> 1-based desktop of window, or -1
//   int VdSetAnimation(BOOL on)      -> previous flag (0/1). Process-global,
//                                      remembered for DLL lifetime (default
//                                      off). Call once after LoadLibrary.
//   int VdLastErrorCode()            -> last HRESULT / Win32 code (0 if none)
//   const char* VdLastErrorText()    -> last error text (UTF-8, thread-local)
//
// Fresh COM session per call: slightly slower (~ms) but immune to
// Explorer-restart staleness and needs no global interface cache.

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <atomic>

#include "vd_com.h"

#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------------------
// Last-error state (per thread, so concurrent AHK threads don't clobber)
// ---------------------------------------------------------------------------

static thread_local int   g_lastCode = 0;
static thread_local char  g_lastText[512] = {0};

static void VdSetError(int code, const char* ctx, HRESULT hr) {
    g_lastCode = code;
    if (hr == 0) hr = (HRESULT)code;
    _snprintf_s(g_lastText, sizeof(g_lastText), _TRUNCATE,
                "%s (code=0x%08lX)", ctx ? ctx : "error",
                (unsigned long)(hr < 0 ? hr : code));
}

static void VdSetErrorMsg(int code, const char* msg) {
    g_lastCode = code;
    _snprintf_s(g_lastText, sizeof(g_lastText), _TRUNCATE, "%s",
                msg ? msg : "error");
}

static void VdClearError() {
    g_lastCode = 0;
    g_lastText[0] = '\0';
}

// ---------------------------------------------------------------------------
// Per-call COM session
// ---------------------------------------------------------------------------

struct VdSession {
    HRESULT hrInit = E_FAIL;
    bool needUninit = false;
    VD_IServiceProvider* shell = nullptr;
    VD_IManagerInternal* mgr = nullptr;
    VD_IViewCollection* views = nullptr;
    VD_IManager* docMgr = nullptr; // documented manager, acquired lazily

    bool InitCom() {
        hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hrInit == RPC_E_CHANGED_MODE) {
            // Caller already runs MTA (or vice versa); usable as-is.
            needUninit = false;
            return true;
        }
        if (SUCCEEDED(hrInit)) {
            needUninit = (hrInit != S_FALSE) ? true : true; // balanced pair
            // S_FALSE = already init on this thread; Uninitialize is still
            // balanced (refcounted), so keep needUninit=true.
            return true;
        }
        VdSetError((int)hrInit, "CoInitializeEx failed", hrInit);
        return false;
    }

    bool Acquire() {
        HRESULT hr = CoCreateInstance(CLSID_VD_ImmersiveShell, nullptr,
                                      CLSCTX_LOCAL_SERVER,
                                      IID_VD_ServiceProvider,
                                      (void**)&shell);
        if (FAILED(hr) || !shell) {
            VdSetError((int)hr, "ImmersiveShell unavailable (wrong Windows build?)", hr);
            return false;
        }
        hr = shell->QueryService(CLSID_VD_ManagerInternal,
                                 IID_VD_ManagerInternal_24H2, (void**)&mgr);
        if (FAILED(hr) || !mgr) {
            VdSetError((int)hr, "IVirtualDesktopManagerInternal mismatch "
                                "(Windows update changed IID/vtable?)", hr);
            return false;
        }
        hr = shell->QueryService(IID_VD_ViewCollection, IID_VD_ViewCollection,
                                 (void**)&views);
        if (FAILED(hr) || !views) {
            VdSetError((int)hr, "IApplicationViewCollection unavailable", hr);
            return false;
        }
        return true;
    }

    // Documented manager is build-stable; only needed for HWND->desktop.
    bool AcquireDoc() {
        if (docMgr) return true;
        HRESULT hr = CoCreateInstance(CLSID_VD_ManagerDocumented, nullptr,
                                      CLSCTX_ALL, IID_VD_ManagerDocumented,
                                      (void**)&docMgr);
        if (FAILED(hr) || !docMgr) {
            VdSetError((int)hr, "IVirtualDesktopManager unavailable", hr);
            return false;
        }
        return true;
    }

    ~VdSession() {
        if (docMgr) docMgr->Release();
        if (views) views->Release();
        if (mgr) mgr->Release();
        if (shell) shell->Release();
        if (needUninit) CoUninitialize();
    }
};

// ---------------------------------------------------------------------------
// Internal helpers (all zero-based; the 1-based mapping lives at the edge)
// ---------------------------------------------------------------------------

static bool VdGetCountRaw(VD_IManagerInternal* mgr, UINT* count) {
    HRESULT hr = mgr->GetCount(count);
    if (FAILED(hr)) {
        VdSetError((int)hr, "GetCount failed", hr);
        return false;
    }
    return true;
}

static bool VdGetDesktopAt(VD_IManagerInternal* mgr, UINT zeroBased,
                           VD_IVirtualDesktop** out) {
    *out = nullptr;
    VD_IObjectArray* arr = nullptr;
    HRESULT hr = mgr->GetDesktops(&arr);
    if (FAILED(hr) || !arr) {
        VdSetError((int)hr, "GetDesktops failed", hr);
        return false;
    }
    hr = arr->GetAt(zeroBased, IID_VD_Desktop_24H2, (void**)out);
    arr->Release();
    if (FAILED(hr) || !*out) {
        VdSetError((int)hr, "GetAt(desktop) failed", hr);
        return false;
    }
    return true;
}

// Find 1-based index of a desktop by GUID. Returns -1 + sets error if lost
// (can happen if the set changed mid-call).
static int VdIndexOfGuid(VD_IManagerInternal* mgr, const GUID& id) {
    VD_IObjectArray* arr = nullptr;
    HRESULT hr = mgr->GetDesktops(&arr);
    if (FAILED(hr) || !arr) {
        VdSetError((int)hr, "GetDesktops failed", hr);
        return -1;
    }
    UINT count = 0;
    if (FAILED(arr->GetCount(&count))) {
        arr->Release();
        VdSetError(E_FAIL, "IObjectArray::GetCount failed", E_FAIL);
        return -1;
    }
    int found = -1;
    for (UINT i = 0; i < count; ++i) {
        VD_IVirtualDesktop* d = nullptr;
        if (FAILED(arr->GetAt(i, IID_VD_Desktop_24H2, (void**)&d)) || !d)
            continue;
        GUID gid = {0};
        HRESULT hrg = d->GetID(&gid);
        d->Release();
        if (SUCCEEDED(hrg) && IsEqualGUID(gid, id)) {
            found = (int)i + 1; // 1-based
            break;
        }
    }
    arr->Release();
    if (found < 0) VdSetErrorMsg(E_FAIL, "desktop no longer exists");
    return found;
}

// Safety cap on total desktops. The underlying COM API is undocumented with
// no MS-published safeguards, so refuse to be a vector for unbounded
// resource-hungry creation. Creation-only: n itself is never clamped and
// already-existing desktops (even beyond the cap, created externally) remain
// fully usable. Only a call that would create a desktop taking the total
// above the cap fails. File an issue if you have a valid use-case for more.
static constexpr UINT kVdMaxDesktops = 32;

// Ensure rule: nOneBased >= 1 required. n <= count -> existing desktop.
// n > count -> CreateDesktop() ONCE; actual becomes oldCount+1, unless
// count is already at kVdMaxDesktops, in which case fail with -1 and set
// VdLastErrorText ("count limit reached") for debugger inspection only
// (the DLL itself never reports to the user).
static bool VdEnsure(VD_IManagerInternal* mgr, int nOneBased,
                     VD_IVirtualDesktop** out, int* actualOneBased) {
    *out = nullptr;
    if (nOneBased < 1) {
        VdSetErrorMsg(E_INVALIDARG, "desktop number starts at 1");
        return false;
    }
    UINT count = 0;
    if (!VdGetCountRaw(mgr, &count)) return false;
    if ((UINT)nOneBased <= count) {
        if (!VdGetDesktopAt(mgr, (UINT)(nOneBased - 1), out)) return false;
        *actualOneBased = nOneBased;
        return true;
    }
    if (count >= kVdMaxDesktops) {
        VdSetErrorMsg(E_FAIL, "CreateDesktop blocked - count limit (32) reached");
        return false;
    }
    HRESULT hr = mgr->CreateDesktop(out);
    if (FAILED(hr) || !*out) {
        VdSetError((int)hr, "CreateDesktop failed", hr);
        return false;
    }
    *actualOneBased = (int)count + 1;
    return true;
}

static bool VdGetForeground(HWND* out) {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        VdSetErrorMsg(E_FAIL, "no focused window");
        return false;
    }
    *out = fg;
    return true;
}

// Resolve the movable application view for hwnd. *viewOut holds a COM
// reference on success (caller releases). Shared by all move paths.
static bool VdResolveMovableView(VdSession& s, HWND hwnd,
                                 VD_ApplicationView** viewOut) {
    *viewOut = nullptr;
    if (!IsWindow(hwnd)) {
        VdSetErrorMsg(E_INVALIDARG, "not a valid window");
        return false;
    }
    HRESULT hr = s.views->GetViewForHwnd(hwnd, viewOut);
    if (FAILED(hr) || !*viewOut) {
        VdSetError((int)hr, "window has no application view "
                            "(toolwindow/shell/elevated?)", hr);
        return false;
    }
    BOOL canMove = FALSE;
    hr = s.mgr->CanViewMoveDesktops(*viewOut, &canMove);
    if (SUCCEEDED(hr) && !canMove) {
        (*viewOut)->Release();
        *viewOut = nullptr;
        VdSetErrorMsg(E_ACCESSDENIED,
                      "window cannot move desktops (pinned/toolwindow?)");
        return false;
    }
    return true;
}

static bool VdMoveHwndInner(VdSession& s, HWND hwnd, int nOneBased,
                            int* actualOneBased) {
    VD_ApplicationView* view = nullptr;
    if (!VdResolveMovableView(s, hwnd, &view)) return false;
    VD_IVirtualDesktop* target = nullptr;
    int actual = 0;
    if (!VdEnsure(s.mgr, nOneBased, &target, &actual)) {
        view->Release();
        return false;
    }
    HRESULT hr = s.mgr->MoveViewToDesktop(view, target);
    view->Release();
    int resultIndex = actual;
    target->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "MoveViewToDesktop failed", hr);
        return false;
    }
    *actualOneBased = resultIndex;
    return true;
}

// Animation preference: process-global (NOT thread-local) so one
// VdSetAnimation call after LoadLibrary covers the whole session.
static std::atomic<bool> g_animate{false};

// Single switch path for VdGoTo/VdGoBack/VdGoLeft/VdGoRight.
static HRESULT VdSwitchTo(VD_IManagerInternal* mgr, VD_IVirtualDesktop* desktop) {
    if (g_animate.load()) {
        // Settle, animate, settle — mirrors the MScholtes MakeVisible order.
        // Waits are best-effort; only the switch itself can fail the call.
        mgr->WaitForAnimationToComplete();
        HRESULT hr = mgr->SwitchDesktopWithAnimation(desktop);
        if (FAILED(hr)) return hr;
        mgr->WaitForAnimationToComplete();
        return S_OK;
    }
    return mgr->SwitchDesktop(desktop);
}

// Switch to the desktop adjacent to the current one.
// direction: 3 = left, 4 = right (matches GetAdjacentDesktop convention).
static int VdGoAdjacent(int direction) {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    VD_IVirtualDesktop* cur = nullptr;
    HRESULT hr = s.mgr->GetCurrentDesktop(&cur);
    if (FAILED(hr) || !cur) {
        VdSetError((int)hr, "GetCurrentDesktop failed", hr);
        return -1;
    }
    VD_IVirtualDesktop* adj = nullptr;
    hr = s.mgr->GetAdjacentDesktop(cur, direction, &adj);
    cur->Release();
    if (FAILED(hr) || !adj) {
        VdSetErrorMsg(E_FAIL, direction == 3 ? "no desktop to the left"
                                             : "no desktop to the right");
        return -1;
    }
    GUID id = {0};
    hr = adj->GetID(&id);
    if (FAILED(hr)) {
        adj->Release();
        VdSetError((int)hr, "GetID failed", hr);
        return -1;
    }
    int idx = VdIndexOfGuid(s.mgr, id);
    if (idx < 0) {
        adj->Release();
        return -1;
    }
    hr = VdSwitchTo(s.mgr, adj);
    adj->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "SwitchDesktop failed", hr);
        return -1;
    }
    return idx;
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) int WINAPI VdGetCount() {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    UINT count = 0;
    if (!VdGetCountRaw(s.mgr, &count)) return -1;
    return (int)count;
}

__declspec(dllexport) int WINAPI VdGetCurrent() {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    VD_IVirtualDesktop* cur = nullptr;
    HRESULT hr = s.mgr->GetCurrentDesktop(&cur);
    if (FAILED(hr) || !cur) {
        VdSetError((int)hr, "GetCurrentDesktop failed", hr);
        return -1;
    }
    GUID id = {0};
    hr = cur->GetID(&id);
    cur->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "GetID failed", hr);
        return -1;
    }
    return VdIndexOfGuid(s.mgr, id);
}

__declspec(dllexport) int WINAPI VdGoTo(int n) {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    VD_IVirtualDesktop* target = nullptr;
    int actual = 0;
    if (!VdEnsure(s.mgr, n, &target, &actual)) return -1;
    HRESULT hr = VdSwitchTo(s.mgr, target);
    target->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "SwitchDesktop failed", hr);
        return -1;
    }
    return actual;
}

__declspec(dllexport) int WINAPI VdMoveFocused(int n) {
    VdClearError();
    HWND fg = nullptr;
    if (!VdGetForeground(&fg)) return -1;
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    int actual = 0;
    if (!VdMoveHwndInner(s, fg, n, &actual)) return -1;
    return actual;
}

__declspec(dllexport) int WINAPI VdMoveHwnd(HWND hwnd, int n) {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    int actual = 0;
    if (!VdMoveHwndInner(s, hwnd, n, &actual)) return -1;
    return actual;
}

// Atomic move+follow: one VdEnsure, then move, then switch. The window and
// focus always land on the same desktop; at most one desktop is created.
__declspec(dllexport) int WINAPI VdMoveFocusedAndGo(int n) {
    VdClearError();
    HWND fg = nullptr;
    if (!VdGetForeground(&fg)) return -1;
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    VD_ApplicationView* view = nullptr;
    if (!VdResolveMovableView(s, fg, &view)) return -1;
    VD_IVirtualDesktop* target = nullptr;
    int actual = 0;
    if (!VdEnsure(s.mgr, n, &target, &actual)) {
        view->Release();
        return -1;
    }
    HRESULT hr = s.mgr->MoveViewToDesktop(view, target);
    view->Release();
    if (FAILED(hr)) {
        target->Release();
        VdSetError((int)hr, "MoveViewToDesktop failed", hr);
        return -1;
    }
    hr = VdSwitchTo(s.mgr, target);
    target->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "SwitchDesktop failed", hr);
        return -1;
    }
    return actual;
}

__declspec(dllexport) int WINAPI VdGetHwndDesktop(HWND hwnd) {
    VdClearError();
    if (!IsWindow(hwnd)) {
        VdSetErrorMsg(E_INVALIDARG, "not a valid window");
        return -1;
    }
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    if (!s.AcquireDoc()) return -1;
    GUID id = {0};
    HRESULT hr = s.docMgr->GetWindowDesktopId(hwnd, &id);
    if (FAILED(hr)) {
        VdSetError((int)hr, "GetWindowDesktopId failed", hr);
        return -1;
    }
    if (IsEqualGUID(id, GUID_VD_AppOnAllDesktops) ||
        IsEqualGUID(id, GUID_VD_WindowOnAllDesktops)) {
        // Pinned: visible everywhere -> report current desktop.
        VD_IVirtualDesktop* cur = nullptr;
        hr = s.mgr->GetCurrentDesktop(&cur);
        if (FAILED(hr) || !cur) {
            VdSetError((int)hr, "GetCurrentDesktop failed", hr);
            return -1;
        }
        GUID cid = {0};
        hr = cur->GetID(&cid);
        cur->Release();
        if (FAILED(hr)) {
            VdSetError((int)hr, "GetID failed", hr);
            return -1;
        }
        return VdIndexOfGuid(s.mgr, cid);
    }
    return VdIndexOfGuid(s.mgr, id);
}

__declspec(dllexport) int WINAPI VdGoBack() {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    // Preferred: last-active desktop, if it still exists.
    int idx = -1;
    VD_IVirtualDesktop* last = nullptr;
    HRESULT hr = s.mgr->GetLastActiveDesktop(&last);
    if (SUCCEEDED(hr) && last) {
        GUID id = {0};
        if (SUCCEEDED(last->GetID(&id)))
            idx = VdIndexOfGuid(s.mgr, id); // -1 if deleted since
        last->Release();
    }
    if (idx < 0) {
        // Fallback: desktop 1 always exists. Resolved positionally, so a
        // replacement "1" works fine. Drop the stale lookup error: the
        // fallback below reports its own failures.
        VdClearError();
        idx = 1;
    }
    VD_IVirtualDesktop* target = nullptr;
    if (!VdGetDesktopAt(s.mgr, (UINT)(idx - 1), &target)) return -1;
    hr = VdSwitchTo(s.mgr, target);
    target->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "SwitchDesktop failed", hr);
        return -1;
    }
    return idx;
}

__declspec(dllexport) int WINAPI VdGoLeft() {
    return VdGoAdjacent(3);
}

__declspec(dllexport) int WINAPI VdGoRight() {
    return VdGoAdjacent(4);
}

__declspec(dllexport) int WINAPI VdRemoveCurrentDesktop() {
    VdClearError();
    VdSession s;
    if (!s.InitCom()) return -1;
    if (!s.Acquire()) return -1;
    UINT count = 0;
    if (!VdGetCountRaw(s.mgr, &count)) return -1;
    VD_IVirtualDesktop* cur = nullptr;
    HRESULT hr = s.mgr->GetCurrentDesktop(&cur);
    if (FAILED(hr) || !cur) {
        VdSetError((int)hr, "GetCurrentDesktop failed", hr);
        return -1;
    }
    GUID curId = {0};
    hr = cur->GetID(&curId);
    if (FAILED(hr)) {
        cur->Release();
        VdSetError((int)hr, "GetID failed", hr);
        return -1;
    }
    int curIdx = VdIndexOfGuid(s.mgr, curId);
    if (curIdx < 0) {
        cur->Release();
        return -1;
    }
    if (count <= 1) {
        // Nothing to remove (Windows would refuse) — silent no-op.
        cur->Release();
        VdClearError();
        return curIdx; // == 1
    }
    // Fallback for the removed desktop's windows: last-active desktop, but
    // only if it still exists and isn't the one being removed (a deleted
    // last-active desktop leaves a stale pointer that RemoveDesktop would
    // reject). Otherwise desktop 1 — or 2 if current is 1 — resolved
    // positionally, so a replacement "1" works fine.
    VD_IVirtualDesktop* fallback = nullptr;
    VD_IVirtualDesktop* last = nullptr;
    hr = s.mgr->GetLastActiveDesktop(&last);
    if (SUCCEEDED(hr) && last) {
        GUID lastId = {0};
        bool live = SUCCEEDED(last->GetID(&lastId)) &&
                    !IsEqualGUID(lastId, curId) &&
                    VdIndexOfGuid(s.mgr, lastId) > 0;
        if (live) {
            fallback = last;
        }
        else {
            last->Release();
        }
    }
    if (!fallback) {
        if (!VdGetDesktopAt(s.mgr, (curIdx == 1) ? 1u : 0u, &fallback)) {
            cur->Release();
            return -1;
        }
    }
    hr = s.mgr->RemoveDesktop(cur, fallback);
    cur->Release();
    fallback->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "RemoveDesktop failed", hr);
        return -1;
    }
    // Succeeded — drop any stale note from the fallback validation above;
    // the landing lookup below reports its own failures.
    VdClearError();
    // Indices shift after removal — report where we actually landed.
    VD_IVirtualDesktop* now = nullptr;
    hr = s.mgr->GetCurrentDesktop(&now);
    if (FAILED(hr) || !now) {
        VdSetError((int)hr, "GetCurrentDesktop failed", hr);
        return -1;
    }
    GUID nowId = {0};
    hr = now->GetID(&nowId);
    now->Release();
    if (FAILED(hr)) {
        VdSetError((int)hr, "GetID failed", hr);
        return -1;
    }
    return VdIndexOfGuid(s.mgr, nowId);
}

__declspec(dllexport) int WINAPI VdSetAnimation(BOOL on) {
    VdClearError();
    bool prev = g_animate.exchange(on != FALSE);
    return prev ? 1 : 0;
}

__declspec(dllexport) int WINAPI VdLastErrorCode() {
    return g_lastCode;
}

__declspec(dllexport) const char* WINAPI VdLastErrorText() {
    return g_lastText;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls((HMODULE)nullptr);
        // Balance: each export pairs CoInitializeEx/CoUninitialize per call,
        // so nothing to do here.
    }
    return TRUE;
}
