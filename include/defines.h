#ifndef DEFINES_H
#define DEFINES_H

#include <windows.h>

// Globals.
extern volatile char isDisabled;
extern volatile char fixerDied;
extern volatile TCHAR* errorMsg;

// main.c
void InitializeWindows(HINSTANCE instanceHandle);
void RegisterMainWindowClass(HINSTANCE instanceHandle);
void UninitializeWindows(HINSTANCE instanceHandle);
char CheckOneInstance();
LRESULT CALLBACK MainWindowProcedure(HWND windowHandle, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT ProcessMainWindowCommand(HWND windowHandle, WPARAM wparam, LPARAM lparam);
void AddNotificationIcon(HWND windowHandle);
void RemoveNotificationIcon(HWND windowHandle);
void ShowContextMenu(HWND hwnd, POINT pt);
void Error(TCHAR* msg);

// fixer.c
void* FixerLoop(void* arg);
void FetchTempFilesPath(WCHAR* buffer, DWORD bufsz, rsize_t countof);
char IsInstantReplayOn(WCHAR* tempFilesPath);
INPUT* FetchToggleShortcut(UINT* arraySize);
void CreateInput(INPUT* input, WORD vkey, char isDown);
void ToggleInstantReplay();
void ThreadError(TCHAR* msg);

#endif