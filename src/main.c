// ShadowplayFixer - a program for modifying the weights of different frequencies in a wave file.
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

#pragma region Macros

#define PROGRAM_NAME TEXT("ShadowplayFixer")

// Takes a notification code and returns it as an HMENU that uses the high word so it works the same as system notification codes.
#define NOTIF_CODIFY(x) MAKEWPARAM(0, x)

// The value in the HIWORD of wparam when windows sends a message about a shortcut from the accelerator table being pressed.
#define ACCELERATOR_SHORTCUT_PRESSED 1

// The WindowClass name of the main window.
#define WC_MAINWINDOW TEXT("MainWindow")

// Notification code for the tray icon. 0x8001 is used by TOGGLE_ACTIVE.
#define TRAY_ICON_CALLBACK 0x8002

// The UUID of the notification icon.
#define TRAY_ICON_UUID 0x69

#pragma endregion // Macros.

static HWND mainWindowHandle = NULL;
static HICON programIcon = NULL;

#pragma region Initialization

// Trying to use wWinMain causes the program to not compile. It's ok though, because we've got GetCommandLine() to get the line as unicode.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    fprintf(stderr, "\n~~~STARTING A RUN~~~\n");

    InitializeWindows(hInstance);
    HACCEL acceleratorHandle = LoadAccelerators(hInstance, MAKEINTRESOURCE(ACCELERATOR_TABLE_ID));
    MSG msg = {0};

    // Entering our message loop.
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(mainWindowHandle, acceleratorHandle, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
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
            AddNotificationIcon(windowHandle);
            return 0;
        case WM_COMMAND:
            return ProcessMainWindowCommand(windowHandle, wparam, lparam);
        case WM_CLOSE:
            RemoveNotificationIcon(windowHandle);
            DestroyWindow(mainWindowHandle);
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(windowHandle, msg, wparam, lparam);
    }
}

LRESULT ProcessMainWindowCommand(HWND windowHandle, WPARAM wparam, LPARAM lparam)
{
    switch (HIWORD(wparam))
    {
        case ACCELERATOR_SHORTCUT_PRESSED:
        
            // Generating a menu button pressed event same as for BN_CLICKED, but only when this is the active window.
            if (GetActiveWindow() == windowHandle)
            {
                ProcessMainWindowCommand(windowHandle, NOTIF_CODIFY(LOWORD(wparam)), lparam);
            }

            break;
        case TOGGLE_ACTIVE:
            RemoveNotificationIcon(windowHandle);
            fprintf(stderr, "toggle\n");
            // TODO: toggle.
            break;
        default:
            break;
    }

    return 0;
}

void AddNotificationIcon(HWND windowHandle)
{
    // TODO: add menu with options to toggle and close.
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = windowHandle;
    nid.uFlags = NIF_ICON | NIF_SHOWTIP | NIF_TIP | NIF_MESSAGE;
    nid.uID = TRAY_ICON_UUID;
    nid.uCallbackMessage = TRAY_ICON_CALLBACK;
    nid.hIcon = programIcon;
    _tcscpy_s(nid.szTip, sizeof(nid.szTip), PROGRAM_NAME);

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

#pragma endregion // MainWindow.
