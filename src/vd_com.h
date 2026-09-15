// vd_com.h — from-scratch COM declarations for Windows 11 24H2/25H2+
// virtual desktops. No dependency on any upstream project; only the
// vtable shapes/IIDs (reverse-engineered, undocumented) are mirrored here.
//
// Target: build 26100+ (24H2) and 26200+ (25H2), which share:
//   IVirtualDesktopManagerInternal {53F5CA0B-158F-4124-900C-057158060B27}
//   IVirtualDesktop                {3F07F4BE-B107-441A-AF0F-39D82529072C}
//
// If a future Windows update breaks this, ONLY this file needs updating:
// re-dump twinui.pcshell.dll (OleViewDotNet / WinJump dump script),
// fix the IID and/or method order below, rebuild. Nothing else changes.

#pragma once

#include <windows.h>
#include <objbase.h>

// ---------------------------------------------------------------------------
// GUIDs (fixed service locations; the *interface* IIDs are what break)
// ---------------------------------------------------------------------------

static const GUID CLSID_VD_ImmersiveShell =
    {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
static const GUID CLSID_VD_ManagerInternal =
    {0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
static const GUID CLSID_VD_ManagerDocumented =
    {0xAA509086, 0x5CA9, 0x4C25, {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}};

static const GUID IID_VD_ServiceProvider =
    {0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};
static const GUID IID_VD_ObjectArray =
    {0x92CA9DCD, 0x5622, 0x4BBA, {0xA8, 0x05, 0x5E, 0x9F, 0x54, 0x1B, 0xD8, 0xC9}};
static const GUID IID_VD_ViewCollection =
    {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

// 24H2/25H2 interface IDs (THE fragile part — see header comment).
static const GUID IID_VD_ManagerInternal_24H2 =
    {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
static const GUID IID_VD_Desktop_24H2 =
    {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};
static const GUID IID_VD_ManagerDocumented =
    {0xA5CD92FF, 0x29BE, 0x454C, {0x8D, 0x04, 0xD8, 0x28, 0x79, 0xFB, 0x3F, 0x1B}};

// "Pinned = visible on all desktops" sentinel GUIDs returned by
// IVirtualDesktopManager::GetWindowDesktopId.
static const GUID GUID_VD_AppOnAllDesktops =
    {0xBB64D5B7, 0x4DE3, 0x4AB2, {0xA8, 0x7C, 0xDB, 0x76, 0x01, 0xAE, 0xA7, 0xDC}};
static const GUID GUID_VD_WindowOnAllDesktops =
    {0xC2DDEA68, 0x66F2, 0x4CF9, {0x82, 0x64, 0x1B, 0xFD, 0x00, 0xFB, 0xBB, 0xAC}};

// HSTRING is pointer-sized; we never call name/wallpaper slots, so an
// opaque pointer keeps us free of <winstring.h> version skew.
typedef void* VD_HSTRING;

// ---------------------------------------------------------------------------
// Minimal interfaces. Only slots we actually CALL must be exactly right,
// but the full order is kept so the vtable indexes stay correct.
// ---------------------------------------------------------------------------

struct VD_ApplicationView : public IUnknown {
    // Opaque: we only pass pointers between GetViewForHwnd and
    // MoveViewToDesktop. No methods declared on purpose.
};

struct VD_IObjectArray : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;              // 3
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT index, REFIID iid,            // 4
                                            void** out) = 0;
};

struct VD_IServiceProvider : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryService(REFGUID service,            // 3
                                                   REFIID riid,
                                                   void** out) = 0;
};

struct VD_IVirtualDesktop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(VD_ApplicationView* view,  // 3
                                                    BOOL* visible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID* id) = 0;                    // 4
    virtual HRESULT STDMETHODCALLTYPE GetName(VD_HSTRING* name) = 0;          // 5
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(VD_HSTRING* path) = 0; // 6
    virtual HRESULT STDMETHODCALLTYPE IsRemote(BOOL* remote) = 0;             // 7
};

struct VD_IViewCollection : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetViews(VD_IObjectArray** out) = 0;              // 3
    virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(VD_IObjectArray** out) = 0;      // 4
    virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(                         // 5
        LPCWSTR id, VD_IObjectArray** out) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND hwnd,                         // 6
                                                     VD_ApplicationView** view) = 0;
    // Slots below are never called; kept as placeholders to document order.
    virtual HRESULT STDMETHODCALLTYPE GetViewForApplication(IUnknown* app,               // 7
                                                            VD_ApplicationView** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForAppUserModelId(                         // 8
        LPCWSTR id, VD_ApplicationView** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewInFocus(VD_ApplicationView** v) = 0;       // 9
    virtual HRESULT STDMETHODCALLTYPE Unknown1(void* p) = 0;                            // 10
    virtual HRESULT STDMETHODCALLTYPE RefreshCollection() = 0;                          // 11
    virtual HRESULT STDMETHODCALLTYPE RegisterForViewChanges(IUnknown* l,               // 12
                                                             int* cookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterForViewChanges(int cookie) = 0;         // 13
};

// 24H2/25H2 layout. NOTE slot 10 (SwitchDesktopAndMoveForegroundView) is the
// method Microsoft inserted in 24H2 that broke all older vtable copies.
struct VD_IManagerInternal : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;                        // 3
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(VD_ApplicationView* view,       // 4
                                                        VD_IVirtualDesktop* desk) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(VD_ApplicationView* view,     // 5
                                                          BOOL* canMove) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(VD_IVirtualDesktop** d) = 0;    // 6
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(VD_IObjectArray** out) = 0;           // 7
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(VD_IVirtualDesktop* from,      // 8
                                                         int direction,
                                                         VD_IVirtualDesktop** out) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(VD_IVirtualDesktop* desktop) = 0;   // 9
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(               // 10 NEW 24H2
        VD_IVirtualDesktop* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(VD_IVirtualDesktop** out) = 0;      // 11
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(VD_IVirtualDesktop* desktop,          // 12
                                                  int indexZeroBased) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(VD_IVirtualDesktop* remove,         // 13
                                                    VD_IVirtualDesktop* fallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID* id,                       // 14
                                                  VD_IVirtualDesktop** out) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(              // 15
        VD_IVirtualDesktop* d, VD_IObjectArray** a, VD_IObjectArray** b) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName(VD_IVirtualDesktop* d,             // 16
                                                     VD_HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(VD_IVirtualDesktop* d,        // 17
                                                          VD_HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(                // 18
        VD_HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(VD_ApplicationView* a,           // 19
                                                       VD_ApplicationView* b) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRemoteDesktop(VD_HSTRING name,              // 20
                                                          VD_IVirtualDesktop** d) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchRemoteDesktop(VD_IVirtualDesktop* d,        // 21
                                                          int type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopWithAnimation(                       // 22
        VD_IVirtualDesktop* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLastActiveDesktop(                             // 23
        VD_IVirtualDesktop** out) = 0;
    virtual HRESULT STDMETHODCALLTYPE WaitForAnimationToComplete() = 0;                 // 24
};

// Documented, stable across builds. Only GetWindowDesktopId is used
// (to resolve an HWND to a 1-based index).
struct VD_IManager : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(
        HWND hwnd, BOOL* onCurrent) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND hwnd, GUID* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveWindowToDesktop(HWND hwnd, REFGUID id) = 0;
};
