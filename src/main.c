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
#include <windows.h>    // For winapi
#include <stdio.h>		// For printing errors and such.
#include <tchar.h>		// For dealing with unicode and ANSI strings.

#pragma region Macros

// Takes a notification code and returns it as an HMENU that uses the high word so it works the same as system notification codes.
#define NOTIF_CODIFY(x) MAKEWPARAM(0, x)

// The value in the HIWORD of wparam when windows sends a message about a shortcut from the accelerator table being pressed.
#define ACCELERATOR_SHORTCUT_PRESSED 1

// The WindowClass name of the main window.
#define WC_MAINWINDOW TEXT("MainWindow")

#pragma endregion // Macros.

static HWND mainWindowHandle = NULL;
static HICON programIcon = NULL;

#pragma region Initialization

// Trying to use wWinMain causes the program to not compile. It's ok though, because we've got GetCommandLine() to get the line as unicode.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	fprintf(stderr, "\n~~~STARTING A RUN~~~\n");

	if (!InitializeWindows(hInstance))
	{
		return -1;
	}

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

char InitializeWindows(HINSTANCE instanceHandle)
{
	programIcon = LoadIcon(instanceHandle, MAKEINTRESOURCE(PROGRAM_ICON_ID));

	if (!RegisterClasses(instanceHandle))
	{
		return FALSE;
	}

	mainWindowHandle = CreateWindow(WC_MAINWINDOW, TEXT("ShadowplayFixer"), WS_MINIMIZE, 0, 0, 0, 0, NULL, NULL, NULL, NULL);		
	return TRUE;
}

char RegisterClasses(HINSTANCE instanceHandle)
{
	return RegisterMainWindowClass(instanceHandle);
}

char RegisterMainWindowClass(HINSTANCE instanceHandle)
{
	WNDCLASS mainWindowClass = {0};
	mainWindowClass.hInstance = instanceHandle;
	mainWindowClass.lpszClassName = WC_MAINWINDOW;
	mainWindowClass.lpfnWndProc = MainWindowProcedure;
	mainWindowClass.hIcon = programIcon;

	// Registering this class. If it fails, we'll log it and end the program.
	if (!RegisterClass(&mainWindowClass))
	{
		fprintf(stderr, "RegisterClass of main window failed with error code: 0x%lX", GetLastError());
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
            // TODO (not sure if here but somewhere) - create system tray icon with toggle menu option. Start up thread which actually does the program's job.
			return 0;
		case WM_COMMAND:
			return ProcessMainWindowCommand(windowHandle, wparam, lparam);
		case WM_NOTIFY:
			return ProcessNotification(wparam, (LPNMHDR)lparam);
		case WM_CLOSE:
			return DefWindowProc(windowHandle, msg, wparam, lparam); // TODO: close.
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
			// TODO: toggle.
			break;
		default:
			break;
	}

	return 0;
}

#pragma endregion // MainWindow.
