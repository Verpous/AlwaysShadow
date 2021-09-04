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

#pragma endregion // Macros.

volatile char isDisabled;

static HINSTANCE instanceHandle = NULL;
static HWND mainWindowHandle = NULL;
static HICON programIcon = NULL;
static pthread_t fixerThread = 0;

#pragma region Initialization

// Trying to use wWinMain causes the program to not compile. It's ok though, because we've got GetCommandLine() to get the line as unicode.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    fprintf(stderr, "\n~~~STARTING A RUN~~~\n");
    instanceHandle = hInstance;
    isDisabled = FALSE;

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
        exit(1);
    }
}

void UninitializeWindows(HINSTANCE instanceHandle)
{
    UnregisterClass(WC_MAINWINDOW, instanceHandle);
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
                    exit(1);
                }
            }

            AddNotificationIcon(windowHandle);
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
        case WM_CLOSE:
            RemoveNotificationIcon(windowHandle);
            pthread_cancel(fixerThread);
            pthread_join(fixerThread, NULL);
            DestroyWindow(windowHandle);
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
    switch (LOWORD(wparam))
    {
        case TOGGLE_ACTIVE:
            isDisabled = !isDisabled;
            break;
        case PROGRAM_EXIT:
            RemoveNotificationIcon(windowHandle);
            DestroyWindow(windowHandle);
            break;
    }

    return 0;
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
        exit(1);
    }
    
    // NOTIFYICON_VERSION_4 is prefered
    nid.uVersion = NOTIFYICON_VERSION_4;

    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid))
    {
        fprintf(stderr, "Failed to set notification icon version.\n");
        exit(1);
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
    HMENU hMenu = LoadMenu(instanceHandle, MAKEINTRESOURCE(CONTEXT_MENU_ID));

    if (hMenu)
    {
        HMENU hSubMenu = GetSubMenu(hMenu, 0);
        if (hSubMenu)
        {
            // Our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away.
            SetForegroundWindow(windowHandle);

            // If the menu item has checked last time set its state to checked before the menu window shows up.
            if (isDisabled)
            {
                MENUITEMINFO mi = { 0 };
                mi.cbSize = sizeof(MENUITEMINFO);
                mi.fMask = MIIM_STATE;
                mi.fState = MF_CHECKED;
                SetMenuItemInfo(hSubMenu, TOGGLE_ACTIVE, FALSE, &mi);
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
        }

        DestroyMenu(hMenu);
    }
}

#pragma endregion // MainWindow.
