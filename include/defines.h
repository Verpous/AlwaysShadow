#ifndef DEFINES_H
#define DEFINES_H

#include <windows.h>
#include <stdio.h>

#define LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

typedef struct
{
    UINT amount;
    LPTSTR text;
} TimeOption;

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
UINT GetMilliseconds(int id);
SYSTEMTIME AddMillisecondsToTime(const SYSTEMTIME* sysTime, UINT millis);
void AddNotificationIcon(HWND windowHandle);
void RemoveNotificationIcon(HWND windowHandle);
void ShowContextMenu(HWND hwnd, POINT pt);
void ShowEnabledContextMenu(HWND windowHandle, POINT point);
void ShowDisabledContextMenu(HWND windowHandle, POINT point);
void Error(TCHAR* msg);
INT_PTR TimePickerProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void FillListbox(HWND dialog, int id, TimeOption* items, size_t nitems);
int GetSelection(HWND dialog, int id);

// fixer.c
void* FixerLoop(void* arg);
void ThreadError(TCHAR* msg);
void ReleaseResources(void* arg);
char IsInstantReplayOn();

INPUT* FetchToggleShortcut(size_t* ninputs);
void CreateInput(INPUT* input, WORD vkey, char isDown);
void ToggleInstantReplay();

void InitializeWmi();
BSTR* FetchWhitelist(size_t* nprocs);
char WhitelistedProcessIsRunning(BSTR* procCmds, size_t nprocs);
char* StripWhitespace(char* str);
size_t StripWhitespaceBSTR(BSTR bstr, wchar_t** result);

#endif