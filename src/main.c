// AlwaysShadow - a program for forcing Shadowplay's Instant Replay to stay on.
// Copyright (C) 2020 Aviv Edery.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Resource.h"
#include "defines.h"
#include <windows.h>    // For winapi.
#include <stdio.h>      // For printing errors and such.
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.

#pragma region Macros

// The name of this program.
#define PROGRAM_NAME TEXT("AlwaysShadow")

// The WindowClass name of the main window.
#define WC_MAINWINDOW TEXT("MainWindow")

// The UUID of the notification icon.
#define TRAY_ICON_UUID 0x69

// The ID of the timer for checking if the fixer thread has died.
#define CHECK_ALIVE_TIMER_ID 1

// The ID of the timer for enabling AlwaysShadow after a set time.
#define ENABLE_TIMER_ID 2

#pragma endregion // Macros.

volatile char isDisabled = FALSE;
volatile char fixerDied = FALSE;

static HINSTANCE instanceHandle = NULL;
static HWND mainWindowHandle = NULL;
static HICON programIcon = NULL;
static HANDLE eventHandle = NULL;
static pthread_t fixerThread = 0;
static UINT currentTimerDuration = 0;
static SYSTEMTIME timerEndTime = { 0 };

#pragma region Initialization

// Trying to use wWinMain causes the program to not compile. It's ok though, because we've got GetCommandLine() to get the line as unicode.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    fprintf(stderr, "\n~~~STARTING A RUN~~~\n");

    if (!CheckOneInstance())
    {
        Error(TEXT("Only one instance of AlwaysShadow is allowed."));
    }

    instanceHandle = hInstance;

    InitializeWindows(hInstance);
    MSG msg = {0};

    // Entering our message loop.
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UninitializeWindows(hInstance);
    return 0;
}

void InitializeWindows(HINSTANCE instanceHandle)
{
    programIcon = LoadIcon(instanceHandle, MAKEINTRESOURCE(PROGRAM_ICON_ID));
    RegisterMainWindowClass(instanceHandle);
    mainWindowHandle = CreateWindow(WC_MAINWINDOW, PROGRAM_NAME, WS_MINIMIZE, 0, 0, 0, 0, 0, 0, 0, 0);
}

void RegisterMainWindowClass(HINSTANCE instanceHandle)
{
    WNDCLASS mainWindowClass = {0};
    mainWindowClass.hInstance = instanceHandle;
    mainWindowClass.lpszClassName = WC_MAINWINDOW;
    mainWindowClass.lpfnWndProc = MainWindowProcedure;
    mainWindowClass.hIcon = programIcon;

    // Registering this class. If it fails, we'll log it and end the program.
    if (!RegisterClass(&mainWindowClass))
    {
        fprintf(stderr, "RegisterClass of main window failed with error code: 0x%lX\n", GetLastError());
        Error(TEXT("There was an error when initializing the program. Quitting."));
    }
}

void UninitializeWindows(HINSTANCE instanceHandle)
{
    UnregisterClass(WC_MAINWINDOW, instanceHandle);
}

char CheckOneInstance()
{
    eventHandle = CreateEvent(NULL, FALSE, FALSE, TEXT("Global\\AlwaysShadowEvent"));

    if (eventHandle == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(eventHandle);
        eventHandle = NULL;
        return FALSE;
    }

    return TRUE;
}

#pragma endregion // Initialization.

#pragma region MainWindow

LRESULT CALLBACK MainWindowProcedure(HWND windowHandle, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_CREATE:
            {
                int ret;
                if ((ret = pthread_create(&fixerThread, NULL, FixerLoop, NULL)) != 0)
                {
                    fprintf(stderr, "pthread_create failed with error code 0x%X", ret);
                    Error(TEXT("There was an error when initializing the program. Quitting."));
                }
            }

            AddNotificationIcon(windowHandle);
            SetTimer(windowHandle, CHECK_ALIVE_TIMER_ID, 1000, NULL);
            return 0;
        case WM_COMMAND:
            return ProcessMainWindowCommand(windowHandle, wparam, lparam);
        case TRAY_ICON_CALLBACK:
            switch (LOWORD(lparam))
            {
                case NIN_SELECT:
                case WM_CONTEXTMENU:
                    {
                        POINT const pt = { LOWORD(wparam), HIWORD(wparam) };
                        ShowContextMenu(windowHandle, pt);
                    }
                    break;
            }

            return 0;
        case WM_TIMER:
            switch (wparam)
            {
                case CHECK_ALIVE_TIMER_ID:
                    if (fixerDied)
                    {
                        KillTimer(windowHandle, CHECK_ALIVE_TIMER_ID);
                        Error(errorMsg);
                    }

                    break;
                case ENABLE_TIMER_ID:
                    KillTimer(windowHandle, ENABLE_TIMER_ID);
                    isDisabled = FALSE;
                    break;
            }

            return 0;
        case WM_CLOSE:
            RemoveNotificationIcon(windowHandle);
            pthread_cancel(fixerThread);
            pthread_join(fixerThread, NULL);
            DestroyWindow(windowHandle);
            CloseHandle(eventHandle);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(windowHandle, msg, wparam, lparam);
    }
}

LRESULT ProcessMainWindowCommand(HWND windowHandle, WPARAM wparam, LPARAM lparam)
{
    WORD wparamLow = LOWORD(wparam);

    switch (wparamLow)
    {
        case DISABLE_CUSTOM:
            // TODO: this.
            break;
        case DISABLE_15MIN:
        case DISABLE_30MIN:
        case DISABLE_45MIN:
        case DISABLE_1HR:
        case DISABLE_2HR:
        case DISABLE_3HR:
        case DISABLE_4HR:
        case DISABLE_INDEFINITE:
            currentTimerDuration = GetMilliseconds(wparamLow);
            GetLocalTime(&timerEndTime);
            timerEndTime = AddMillisecondsToTime(&timerEndTime, currentTimerDuration);

            if (currentTimerDuration >= USER_TIMER_MINIMUM)
            {
                SetTimer(windowHandle, ENABLE_TIMER_ID, currentTimerDuration, NULL);
            }

            isDisabled = TRUE;
            break;
        case ENABLE_INDEFINITE:
            // If there is no timer it's no harm done.
            KillTimer(windowHandle, ENABLE_TIMER_ID);
            isDisabled = FALSE;
            break;
        case PROGRAM_EXIT:
            RemoveNotificationIcon(windowHandle);
            DestroyWindow(windowHandle);
            break;
    }

    return 0;
}

// Translates a notification code to a milliseconds duration.
UINT GetMilliseconds(int id)
{
    // Everything's measured in milliseconds.
    const UINT minute = 60 * 1000;
    const UINT hour = 60 * minute;

    switch (id)
    {
        case DISABLE_15MIN:
            return 15 * minute;
        case DISABLE_30MIN:
            return 30 * minute;
        case DISABLE_45MIN:
            return 45 * minute;
        case DISABLE_1HR:
            return 1 * hour;
        case DISABLE_2HR:
            return 2 * hour;
        case DISABLE_3HR:
            return 3 * hour;
        case DISABLE_4HR:
            return 4 * hour;
        default:
            return 0; // Important to return something less than USER_TIMER_MINIMUM in this case.
    }
}

SYSTEMTIME AddMillisecondsToTime(const SYSTEMTIME* sysTime, UINT millis)
{
    FILETIME fileTime;
    SYSTEMTIME result;
    SystemTimeToFileTime(sysTime, &fileTime);

    ULARGE_INTEGER largeInt; 
    memcpy(&largeInt, &fileTime, sizeof(largeInt));

    const ULONGLONG millisTo100nanos = 10000;
    largeInt.QuadPart += millis * millisTo100nanos;

    memcpy(&fileTime, &largeInt, sizeof(fileTime));
    FileTimeToSystemTime(&fileTime, &result);
    return result;
}

void AddNotificationIcon(HWND windowHandle)
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = windowHandle;
    nid.uFlags = NIF_ICON | NIF_SHOWTIP | NIF_TIP | NIF_MESSAGE;
    nid.uID = TRAY_ICON_UUID;
    nid.uCallbackMessage = TRAY_ICON_CALLBACK;
    nid.hIcon = programIcon;
    _tcscpy_s(nid.szTip, _countof(nid.szTip), PROGRAM_NAME);

    if (!Shell_NotifyIcon(NIM_ADD, &nid))
    {
        fprintf(stderr, "Failed to create notification icon.\n");
        Error(TEXT("There was an error creating the system tray icon. Quitting."));
    }
    
    // NOTIFYICON_VERSION_4 is preferred
    nid.uVersion = NOTIFYICON_VERSION_4;

    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid))
    {
        fprintf(stderr, "Failed to set notification icon version.\n");
        Error(TEXT("There was an error creating the system tray icon. Quitting."));
    }
}

void RemoveNotificationIcon(HWND windowHandle)
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = windowHandle;
    nid.uFlags = NIF_ICON;
    nid.uID = TRAY_ICON_UUID;
    nid.hIcon = programIcon;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND windowHandle, POINT point)
{
    if (isDisabled)
    {
        ShowDisabledContextMenu(windowHandle, point);
    }
    else
    {
        ShowEnabledContextMenu(windowHandle, point);
    }
}

void ShowEnabledContextMenu(HWND windowHandle, POINT point)
{
    HMENU hMenu = LoadMenu(instanceHandle, MAKEINTRESOURCE(ENABLED_CONTEXT_MENU_ID));

    if (!hMenu)
    {
        return;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
    {
        goto cleanup;
    }

    // Our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away.
    SetForegroundWindow(windowHandle);

    // Respect menu drop alignment.
    UINT uFlags = TPM_RIGHTBUTTON;

    if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
    {
        uFlags |= TPM_RIGHTALIGN;
    }
    else
    {
        uFlags |= TPM_LEFTALIGN;
    }

    TrackPopupMenuEx(hSubMenu, uFlags, point.x, point.y, windowHandle, NULL);

cleanup:
    DestroyMenu(hMenu);
}

void ShowDisabledContextMenu(HWND windowHandle, POINT point)
{
    HMENU hMenu = LoadMenu(instanceHandle, MAKEINTRESOURCE(DISABLED_CONTEXT_MENU_ID));

    if (!hMenu)
    {
        return;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
    {
        goto cleanup;
    }

    // Our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away.
    SetForegroundWindow(windowHandle);

    // If the timer's duration is 0 then it's disabled indefinitely and we don't need to write until when it's disabled.
    if (currentTimerDuration > 0)
    {
        // Writing the text. End result should look like: "Enable AlwaysShadow (disabled until 18:32)"
        TCHAR txt[256];
        _stprintf_s(txt, sizeof(txt) / sizeof(*txt), TEXT("Enable AlwaysShadow (disabled until %u:%02u)"),
            timerEndTime.wHour, timerEndTime.wMinute);

        MENUITEMINFO mi = { 0 };
        mi.cbSize = sizeof(MENUITEMINFO);
        mi.fMask = MIIM_TYPE;
        mi.dwTypeData = txt;
        SetMenuItemInfo(hSubMenu, ENABLE_INDEFINITE, FALSE, &mi);
    }
    
    // Respect menu drop alignment.
    UINT uFlags = TPM_RIGHTBUTTON;

    if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
    {
        uFlags |= TPM_RIGHTALIGN;
    }
    else
    {
        uFlags |= TPM_LEFTALIGN;
    }

    TrackPopupMenuEx(hSubMenu, uFlags, point.x, point.y, windowHandle, NULL);

cleanup:
    DestroyMenu(hMenu);
}

void Error(TCHAR* msg)
{
    MessageBox(mainWindowHandle, msg == NULL ? TEXT("An unidentified error has occured. Quitting.") : msg, PROGRAM_NAME TEXT(" - Error"), MB_OK | MB_ICONERROR);
    exit(1);
}

#pragma endregion // MainWindow.
