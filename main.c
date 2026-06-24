/*
 * battery_tray.c
 *
 * A Windows XP system tray app that shows a live numerical battery
 * percentage instead of the stock Windows battery icon.
 *
 * Targets: Windows XP and later, Dell Latitude D610 (or any laptop
 * exposing standard ACPI battery info through GetSystemPowerStatus).
 *
 * Build with MSVC 2008:
 *   cl battery_tray.c /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS
 *
 * Build with MSYS2 + MinGW-w64 GCC:
 *   gcc battery_tray.c -o battery_tray.exe -luser32 -lgdi32 -lshell32 -mwindows
 *
 * (See accompanying notes for 32-bit-on-XP specific flags.)
 */

#define _WIN32_WINNT 0x0501   /* Target Windows XP (0x0501) APIs explicitly */
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

/* ---- Custom window messages / IDs ------------------------------------- */

#define WM_TRAYICON      (WM_USER + 1)
#define ID_TRAY_ICON     1001
#define ID_TIMER_REFRESH 1
#define ID_MENU_EXIT     2001
#define ID_MENU_REFRESH  2002

#define REFRESH_INTERVAL_MS 15000   /* poll every 15 seconds */

static NOTIFYICONDATA g_nid;
static HWND g_hwnd;
static HINSTANCE g_hInstance;

/* ------------------------------------------------------------------------
 * BuildBatteryIcon
 *
 * Renders the current battery percentage as text onto a small bitmap,
 * then converts that bitmap into an HICON suitable for the tray.
 *
 * We draw at a larger size (e.g. 64x64) and let Windows downscale to the
 * actual small-icon size (typically 16x16), which keeps text reasonably
 * legible versus drawing directly at 16x16.
 * ---------------------------------------------------------------------- */
static HICON BuildBatteryIcon(int percent, BOOL charging, BOOL acOnline, BOOL unknown)
{
    const int SZ = 32; /* working canvas size; scaled down by the OS for tray use */

    HDC screenDC;
    HDC memDC;
    HDC maskDC;
    HBITMAP colorBmp;
    HBITMAP maskBmp;
    HBITMAP oldColorBmp;
    HBITMAP oldMaskBmp;
    RECT full;
    RECT maskFull;
    COLORREF bg;
    COLORREF fg;
    HBRUSH bgBrush;
    HBRUSH blackBrush;
    char text[16];
    HFONT font;
    HFONT oldFont;
    ICONINFO ii;
    HICON hIcon;

    screenDC = GetDC(NULL);
    memDC = CreateCompatibleDC(screenDC);
    maskDC = CreateCompatibleDC(screenDC);

    /* Color (XOR/AND) bitmap and a 1bpp mask bitmap, as required by CreateIconIndirect */
    colorBmp = CreateCompatibleBitmap(screenDC, SZ, SZ);
    maskBmp  = CreateBitmap(SZ, SZ, 1, 1, NULL);

    oldColorBmp = (HBITMAP)SelectObject(memDC, colorBmp);
    oldMaskBmp  = (HBITMAP)SelectObject(maskDC, maskBmp);

    full.left = 0; full.top = 0; full.right = SZ; full.bottom = SZ;

    /* Background color depends on state: gives an at-a-glance cue even
       before reading the number. Plain, high-contrast choices for a
       small icon. */
    fg = RGB(255, 255, 255);

    if (unknown) {
        bg = RGB(96, 96, 96);          /* gray: unknown state */
    } else if (charging) {
        bg = RGB(0, 120, 215);         /* blue: actively charging */
    } else if (percent <= 10) {
        bg = RGB(196, 32, 32);         /* red: critically low */
    } else if (percent <= 25) {
        bg = RGB(210, 140, 0);         /* amber: low */
    } else if (acOnline) {
        bg = RGB(60, 60, 60);          /* dark gray: plugged in, not charging (e.g. full) */
    } else {
        bg = RGB(34, 139, 34);         /* green: discharging normally, healthy level */
    }

    bgBrush = CreateSolidBrush(bg);
    FillRect(memDC, &full, bgBrush);
    DeleteObject(bgBrush);

    /* Mask: all opaque (we draw a filled square, no transparency needed) */
    maskFull.left = 0; maskFull.top = 0; maskFull.right = SZ; maskFull.bottom = SZ;
    blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(maskDC, &maskFull, blackBrush);

    /* Draw the percentage text. Buffer is sized generously; percent is
       always clamped to 0-100 by the caller's data source, but we don't
       trust that blindly here. */
    if (unknown) {
        sprintf(text, "?");
    } else if (percent >= 100) {
        sprintf(text, "100");
    } else if (percent < 0) {
        sprintf(text, "0");
    } else {
        sprintf(text, "%d", percent);
    }

    font = CreateFontA(
        (percent >= 100 || unknown) ? 14 : 18, 0, 0, 0,
        FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
        "Tahoma");

    oldFont = (HFONT)SelectObject(memDC, font);
    SetTextColor(memDC, fg);
    SetBkMode(memDC, TRANSPARENT);
    DrawTextA(memDC, text, -1, &full, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Small charging accent: a filled triangle in the corner, legible at
       tray scale (a precise lightning-bolt outline disappears at 16x16). */
    if (charging) {
        HBRUSH accent = CreateSolidBrush(RGB(255, 230, 0));
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, accent);
        POINT tri[3];
        tri[0].x = 24; tri[0].y = 2;
        tri[1].x = 30; tri[1].y = 2;
        tri[2].x = 24; tri[2].y = 10;
        Polygon(memDC, tri, 3);
        SelectObject(memDC, oldBrush);
        DeleteObject(accent);
    }

    SelectObject(memDC, oldFont);
    DeleteObject(font);

    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = maskBmp;
    ii.hbmColor = colorBmp;

    hIcon = CreateIconIndirect(&ii);

    SelectObject(memDC, oldColorBmp);
    SelectObject(maskDC, oldMaskBmp);
    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
    DeleteDC(memDC);
    DeleteDC(maskDC);
    ReleaseDC(NULL, screenDC);

    return hIcon;
}

/* ------------------------------------------------------------------------
 * UpdateTrayIcon
 *
 * Queries current power status and refreshes the tray icon + tooltip.
 * ---------------------------------------------------------------------- */
static void UpdateTrayIcon(void)
{
    SYSTEM_POWER_STATUS sps;
    BOOL ok;
    int percent = 0;
    BOOL charging = FALSE;
    BOOL acOnline = FALSE;
    BOOL unknown = FALSE;
    char tooltip[128];
    const char *state;
    HICON newIcon;
    HICON oldIcon;

    ok = GetSystemPowerStatus(&sps);

    if (!ok || sps.BatteryFlag == 128 /* BATTERY_FLAG_NO_BATTERY */ ||
        sps.BatteryLifePercent == 255 /* unknown */) {
        unknown = TRUE;
        sprintf(tooltip, "Battery status unavailable");
    } else {
        percent = sps.BatteryLifePercent;
        acOnline = (sps.ACLineStatus == 1);

        /* BatteryFlag bits: 1=high,2=low,4=critical,8=charging,128=no battery */
        charging = (sps.BatteryFlag & 8) ? TRUE : FALSE;

        if (charging) {
            state = "Charging";
        } else if (acOnline) {
            state = "Plugged in, not charging";
        } else {
            state = "On battery";
        }

        sprintf(tooltip, "Battery: %d%% (%s)", percent, state);
    }

    newIcon = BuildBatteryIcon(percent, charging, acOnline, unknown);
    oldIcon = g_nid.hIcon;

    g_nid.hIcon = newIcon;
    /* NOTIFYICONDATA.szTip is fixed-size; use a bounded copy */
    lstrcpynA(g_nid.szTip, tooltip, sizeof(g_nid.szTip) / sizeof(g_nid.szTip[0]));
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

    Shell_NotifyIcon(NIM_MODIFY, &g_nid);

    if (oldIcon) {
        DestroyIcon(oldIcon);
    }
}

/* ------------------------------------------------------------------------
 * WndProc
 * ---------------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        UpdateTrayIcon();
        SetTimer(hwnd, ID_TIMER_REFRESH, REFRESH_INTERVAL_MS, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_REFRESH) {
            UpdateTrayIcon();
        }
        return 0;

    case WM_POWERBROADCAST:
        /* React immediately to power-source / battery status changes
           rather than waiting for the next timer tick. */
        if (wParam == PBT_APMPOWERSTATUSCHANGE) {
            UpdateTrayIcon();
        }
        return TRUE;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            HMENU menu;
            GetCursorPos(&pt);

            menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, ID_MENU_REFRESH, "Refresh now");
            AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(menu, MF_STRING, ID_MENU_EXIT, "Exit");

            /* Required dance for popup menus on a message-only/hidden window */
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);

            DestroyMenu(menu);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            UpdateTrayIcon(); /* quick manual refresh on double-click */
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MENU_EXIT:
            DestroyWindow(hwnd);
            return 0;
        case ID_MENU_REFRESH:
            UpdateTrayIcon();
            return 0;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_REFRESH);
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        if (g_nid.hIcon) {
            DestroyIcon(g_nid.hIcon);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------------
 * WinMain
 * ---------------------------------------------------------------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow)
{
    HWND existing;
    WNDCLASSEXA wc;
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;

    /* Prevent multiple instances by checking for an existing window first */
    existing = FindWindowA("BatteryTrayWndClass", NULL);
    if (existing) {
        MessageBoxA(NULL, "Battery Tray is already running.", "Battery Tray", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "BatteryTrayWndClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class.", "Battery Tray", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* A real (but never shown) window — required to own the tray icon
       and receive its messages, including WM_POWERBROADCAST. */
    g_hwnd = CreateWindowExA(
        0, "BatteryTrayWndClass", "Battery Tray",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL);

    if (!g_hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "Battery Tray", MB_OK | MB_ICONERROR);
        return 1;
    }

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); /* placeholder until first update */
    lstrcpynA(g_nid.szTip, "Battery Tray", sizeof(g_nid.szTip) / sizeof(g_nid.szTip[0]));

    Shell_NotifyIcon(NIM_ADD, &g_nid);

    /* WM_CREATE already fired during CreateWindowExA, but the tray icon
       didn't exist yet at that point, so do the first real update now. */
    UpdateTrayIcon();

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
