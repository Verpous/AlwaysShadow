// AlwaysShadow - a program for forcing Shadowplay's Instant Replay to stay on.
// Copyright (C) 2023 Aviv Edery.

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
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.
#include <shlobj.h>     // For getting AppData path.
#include <direct.h>     // For making log file directory.
#include <errno.h>      // For handling mkdir errors.
#include <share.h>      // For opening a file with sharing options.
#include <time.h>       // For logging date & time.

#pragma region Declarations

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

#define MAKE_TIME_OPTION(t) { .amount = t, .text = TEXT(#t) }

#define MILLIS_PER_SECOND (1000u)
#define MILLIS_PER_MINUTE (60u * MILLIS_PER_SECOND)
#define MILLIS_PER_HOUR (60u * MILLIS_PER_MINUTE)

typedef struct
{
    UINT amount;
    LPTSTR text;
} TimeOption;

typedef struct
{
    HINSTANCE instanceHandle;
    HWND mainWindowHandle;
    HICON programIcon;
    HANDLE eventHandle;
    pthread_t fixerThread;
    UINT currentTimerDuration;
    SYSTEMTIME timerEndTime;
    BOOL inDialog;
} MainCb;

static void InitializeLogging();
static void InitializeWindows(HINSTANCE instanceHandle);
static void RegisterMainWindowClass(HINSTANCE instanceHandle);
static void UninitializeWindows(HINSTANCE instanceHandle);
static char CheckOneInstance();
static LRESULT CALLBACK MainWindowProcedure(HWND windowHandle, UINT msg, WPARAM wparam, LPARAM lparam);
static LRESULT ProcessMainWindowCommand(HWND windowHandle, WPARAM wparam, LPARAM lparam);
static UINT GetMilliseconds(int id);
static SYSTEMTIME AddMillisecondsToTime(const SYSTEMTIME *sysTime, UINT millis);
static void AddNotificationIcon(HWND windowHandle);
static void RemoveNotificationIcon(HWND windowHandle);
static void ShowContextMenu(HWND hwnd, POINT pt);
static void ShowEnabledContextMenu(HWND windowHandle, POINT point);
static void ShowDisabledContextMenu(HWND windowHandle, POINT point);
static void Panic(LPTSTR msg);
static void Warn(LPTSTR msg);
static INT_PTR TimePickerProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
static void FillListbox(HWND dialog, int id, const TimeOption *items, size_t nitems);
static int GetSelection(HWND dialog, int id);

#pragma endregion // Declarations.

#pragma region Variables

const TimeOption seconds[] =
{
    MAKE_TIME_OPTION(0), MAKE_TIME_OPTION(5), MAKE_TIME_OPTION(10), MAKE_TIME_OPTION(15),MAKE_TIME_OPTION(20), MAKE_TIME_OPTION(25),
    MAKE_TIME_OPTION(30), MAKE_TIME_OPTION(35), MAKE_TIME_OPTION(40), MAKE_TIME_OPTION(45), MAKE_TIME_OPTION(50), MAKE_TIME_OPTION(55),
};

const TimeOption minutes[] = 
{
    MAKE_TIME_OPTION(0), MAKE_TIME_OPTION(5), MAKE_TIME_OPTION(10), MAKE_TIME_OPTION(15),MAKE_TIME_OPTION(20), MAKE_TIME_OPTION(25),
    MAKE_TIME_OPTION(30), MAKE_TIME_OPTION(35), MAKE_TIME_OPTION(40), MAKE_TIME_OPTION(45), MAKE_TIME_OPTION(50), MAKE_TIME_OPTION(55),
};

const TimeOption hours[] =
{
    MAKE_TIME_OPTION(0), MAKE_TIME_OPTION(1), MAKE_TIME_OPTION(2), MAKE_TIME_OPTION(3), MAKE_TIME_OPTION(4),
    MAKE_TIME_OPTION(5), MAKE_TIME_OPTION(6), MAKE_TIME_OPTION(7), MAKE_TIME_OPTION(8), MAKE_TIME_OPTION(9),
    MAKE_TIME_OPTION(10), MAKE_TIME_OPTION(11), MAKE_TIME_OPTION(12), MAKE_TIME_OPTION(13), MAKE_TIME_OPTION(14),
    MAKE_TIME_OPTION(15), MAKE_TIME_OPTION(16), MAKE_TIME_OPTION(17), MAKE_TIME_OPTION(18), MAKE_TIME_OPTION(19),
    MAKE_TIME_OPTION(20), MAKE_TIME_OPTION(21), MAKE_TIME_OPTION(22), MAKE_TIME_OPTION(23),
};

GlobalCb glbl =
{
    .isDisabled = FALSE,
    .isRefresh = FALSE,
    .fixerDied = FALSE,
    .issueWarning = FALSE,
    .errorMsg = {0},
    .warningMsg = {0},
    .lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER,

    .logfile = NULL,
    .loglock = PTHREAD_ONCE_INIT,
};

static MainCb cb = {0};

#pragma endregion // Variables.

#pragma region Initialization

// Trying to use wWinMain causes the program to not compile. It's ok though, because we've got GetCommandLine() to get the line as unicode.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    if (!CheckOneInstance())
    {
        PANIC(TEXT("Only one instance of the program is allowed."));
    }

    // The log file is a shared resource so we can't initialize it until we've ensured we're the only instance.
    InitializeLogging();
    LOG("~~~~ STARTING A RUN: BUILD DATE %s %s ~~~~", __DATE__, __TIME__);

    cb.instanceHandle = hInstance;

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

static void InitializeLogging()
{
    // Default to this unless assigned otherwise.
    glbl.logfile = stderr;

    // Get local app data path.
    wchar_t *localAppDataPath;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &localAppDataPath);

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to obtain LocalAppData path with error code %#lx, using stderr.", hr);
        goto exit;
    }

    // Make directory in %LOCALAPPDATA% where we'll store the logs.
    int res;
    wchar_t logfileName[1 << 13];
    swprintf_s(logfileName, _countof(logfileName), L"%ls\\AlwaysShadow", localAppDataPath);

    if ((res = _wmkdir(logfileName)) != 0 && errno != EEXIST)
    {
        LOG_ERROR("Failed to make directory for log file with error %s", strerror(errno));
        goto exit;
    }

    // Open log file in the path we've created.
    FILE *file;
    swprintf_s(logfileName, _countof(logfileName), L"%ls\\AlwaysShadow\\output.log", localAppDataPath);

    // Kinda hacky: to prevent the log file from growing forever, we have a random chance to open the file in write mode instead of append,
    // which truncates the file and behaves similar enough to append for all we care.
    srand(time(NULL));
    LPWSTR mode = rand() % 100 < 5 ? L"w" : L"a";

    // We allow reading of the log file while it is open.
    if ((file = _wfsopen(logfileName, mode, _SH_DENYWR)) == NULL)
    {
        LOG_ERROR("Failed to make log file with error %s", strerror(errno));
        goto exit;
    }

    glbl.logfile = file;

exit:
    CoTaskMemFree(localAppDataPath);
    return;
}

// Do not hold on to the returned string as it will change next time you call this function.
char *GetDateTimeStr()
{
    static __thread char str[1 << 8];
    time_t timestamp = time(NULL);
    struct tm timeData;
    localtime_s(&timeData, &timestamp); // localtime is not thread safe!
    strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &timeData);
    str[_countof(str) - 1] = '\0';
    return str;
}

static void InitializeWindows(HINSTANCE instanceHandle)
{
    cb.programIcon = LoadIcon(instanceHandle, MAKEINTRESOURCE(PROGRAM_ICON_ID));
    RegisterMainWindowClass(instanceHandle);
    cb.mainWindowHandle = CreateWindow(WC_MAINWINDOW, PROGRAM_NAME, WS_MINIMIZE, 0, 0, 0, 0, 0, 0, 0, 0);
}

static void RegisterMainWindowClass(HINSTANCE instanceHandle)
{
    WNDCLASS mainWindowClass = {0};
    mainWindowClass.hInstance = instanceHandle;
    mainWindowClass.lpszClassName = WC_MAINWINDOW;
    mainWindowClass.lpfnWndProc = MainWindowProcedure;
    mainWindowClass.hIcon = cb.programIcon;

    // Registering this class. If it fails, we'll log it and end the program.
    if (!RegisterClass(&mainWindowClass))
    {
        LOG_ERROR("RegisterClass of main window failed with error code: %#lx", GetLastError());
        PANIC(TEXT("Error initializing the program: RegisterClass error %#lx. Quitting."), GetLastError());
    }
}

static void UninitializeWindows(HINSTANCE instanceHandle)
{
    UnregisterClass(WC_MAINWINDOW, instanceHandle);
}

// IMPORTANT: This function cannot use LOG because it is called before logging is initialized.
static char CheckOneInstance()
{
    cb.eventHandle = CreateEvent(NULL, FALSE, FALSE, TEXT("Global\\AlwaysShadowEvent"));

    if (cb.eventHandle == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(cb.eventHandle);
        cb.eventHandle = NULL;
        return FALSE;
    }

    return TRUE;
}

#pragma endregion // Initialization.

#pragma region MainWindow

static LRESULT CALLBACK MainWindowProcedure(HWND windowHandle, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_CREATE:
            {
                int ret;
                if ((ret = pthread_create(&cb.fixerThread, NULL, FixerLoop, NULL)) != 0)
                {
                    LOG_ERROR("pthread_create failed with error code %#x", ret);
                    PANIC(TEXT("Error initializing the program: pthread_create error %#x. Quitting."), ret);
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
                    // I want to avoid holding the lock while displaying message boxes that can last very long.
                    pthread_mutex_lock(&glbl.lock);
                    char fixerDied = glbl.fixerDied;
                    char issueWarning = glbl.issueWarning;
                    pthread_mutex_unlock(&glbl.lock);

                    // If fixer died then there is no second thread so we need not worry about locking for errorMsg.
                    if (fixerDied)
                    {
                        LOG("Main thread found that fixer died. Quitting.");
                        KillTimer(windowHandle, CHECK_ALIVE_TIMER_ID);
                        PANIC(TCS_FMT, glbl.errorMsg);
                    }
                    else if (issueWarning) {
                        pthread_mutex_lock(&glbl.lock);
                        glbl.issueWarning = FALSE;
                        WARN(&glbl.lock, TCS_FMT, glbl.warningMsg);
                    }

                    break;
                case ENABLE_TIMER_ID:
                    LOG("Timer has run out, re-enabling.");
                    KillTimer(windowHandle, ENABLE_TIMER_ID);

                    pthread_mutex_lock(&glbl.lock);
                    glbl.isDisabled = FALSE;
                    pthread_mutex_unlock(&glbl.lock);
                    break;
            }

            return 0;
        case WM_CLOSE:
            LOG("Received WM_CLOSE. Quitting.");
            RemoveNotificationIcon(windowHandle);
            pthread_cancel(cb.fixerThread);
            pthread_join(cb.fixerThread, NULL);
            CloseHandle(cb.eventHandle);
            DestroyWindow(windowHandle);
            return 0;
        case WM_DESTROY:
            LOG("Received WM_DESTROY. Quitting.");
            pthread_mutex_lock(&glbl.loglock);
            fflush(glbl.logfile);
            pthread_mutex_unlock(&glbl.loglock);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(windowHandle, msg, wparam, lparam);
    }
}

static LRESULT ProcessMainWindowCommand(HWND windowHandle, WPARAM wparam, LPARAM lparam)
{
    WORD wparamLow = LOWORD(wparam);

    switch (wparamLow)
    {
        case DISABLE_CUSTOM:
            cb.inDialog = TRUE;
            cb.currentTimerDuration = DialogBox(cb.instanceHandle, MAKEINTRESOURCE(TIME_PICKER_ID), windowHandle, TimePickerProc);
            cb.inDialog = FALSE;

            if (cb.currentTimerDuration > USER_TIMER_MINIMUM)
            {
                GetLocalTime(&cb.timerEndTime);
                cb.timerEndTime = AddMillisecondsToTime(&cb.timerEndTime, cb.currentTimerDuration);
                SetTimer(windowHandle, ENABLE_TIMER_ID, cb.currentTimerDuration, NULL);

                pthread_mutex_lock(&glbl.lock);
                glbl.isDisabled = TRUE;
                pthread_mutex_unlock(&glbl.lock);
                
                LOG("Disabled self for custom duration of %d millis", cb.currentTimerDuration);
            }
            else 
            {
                LOG_WARN("Not disabling self because custom duration of %d millis is below the minimum", cb.currentTimerDuration);
            }

            break;
        case DISABLE_15MIN:
        case DISABLE_30MIN:
        case DISABLE_45MIN:
        case DISABLE_1HR:
        case DISABLE_2HR:
        case DISABLE_3HR:
        case DISABLE_4HR:
        case DISABLE_INDEFINITE:
            cb.currentTimerDuration = GetMilliseconds(wparamLow);
            GetLocalTime(&cb.timerEndTime);
            cb.timerEndTime = AddMillisecondsToTime(&cb.timerEndTime, cb.currentTimerDuration);
            LOG("Received disable self request code %#x, millis = %d.", wparamLow, cb.currentTimerDuration);

            if (cb.currentTimerDuration >= USER_TIMER_MINIMUM)
            {
                SetTimer(windowHandle, ENABLE_TIMER_ID, cb.currentTimerDuration, NULL);
                LOG("Timer has been scheduled to re-enable when the time is up.");
            }

            pthread_mutex_lock(&glbl.lock);
            glbl.isDisabled = TRUE;
            pthread_mutex_unlock(&glbl.lock);
            break;
        case ENABLE_INDEFINITE:
            // If there is no timer it's no harm done.
            KillTimer(windowHandle, ENABLE_TIMER_ID);
            LOG("Received enable request, re-enabling.");

            pthread_mutex_lock(&glbl.lock);
            glbl.isDisabled = FALSE;
            pthread_mutex_unlock(&glbl.lock);
            break;
        case PROGRAM_EXIT:
            LOG("Exit button has been pressed. Quitting.");
            RemoveNotificationIcon(windowHandle);
            DestroyWindow(windowHandle);
            break;
        case PROGRAM_REFRESH:
            LOG("Refresh button has been pressed.");

            // Flushing the logs on refresh.
            pthread_mutex_lock(&glbl.loglock);
            fflush(glbl.logfile);
            pthread_mutex_unlock(&glbl.loglock);

            // Marking refresh for the fixer thread to detect.
            pthread_mutex_lock(&glbl.lock);
            glbl.isRefresh = TRUE;
            pthread_mutex_unlock(&glbl.lock);
            break;
    }

    return 0;
}

// Translates a notification code to a milliseconds duration.
static UINT GetMilliseconds(int id)
{
    switch (id)
    {
        case DISABLE_15MIN:
            return 15 * MILLIS_PER_MINUTE;
        case DISABLE_30MIN:
            return 30 * MILLIS_PER_MINUTE;
        case DISABLE_45MIN:
            return 45 * MILLIS_PER_MINUTE;
        case DISABLE_1HR:
            return 1 * MILLIS_PER_HOUR;
        case DISABLE_2HR:
            return 2 * MILLIS_PER_HOUR;
        case DISABLE_3HR:
            return 3 * MILLIS_PER_HOUR;
        case DISABLE_4HR:
            return 4 * MILLIS_PER_HOUR;
        default:
            return 0; // Important to return something less than USER_TIMER_MINIMUM in this case.
    }
}

static SYSTEMTIME AddMillisecondsToTime(const SYSTEMTIME *sysTime, UINT millis)
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

static void AddNotificationIcon(HWND windowHandle)
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = windowHandle;
    nid.uFlags = NIF_ICON | NIF_SHOWTIP | NIF_TIP | NIF_MESSAGE;
    nid.uID = TRAY_ICON_UUID;
    nid.uCallbackMessage = TRAY_ICON_CALLBACK;
    nid.hIcon = cb.programIcon;
    _tcscpy_s(nid.szTip, _countof(nid.szTip), PROGRAM_NAME);

    if (!Shell_NotifyIcon(NIM_ADD, &nid))
    {
        LOG_ERROR("Failed to create notification icon.");
        PANIC(TEXT("Error creating the system tray icon. Quitting."));
    }
    
    // NOTIFYICON_VERSION_4 is preferred
    nid.uVersion = NOTIFYICON_VERSION_4;

    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid))
    {
        LOG_ERROR("Failed to set notification icon version.");
        PANIC(TEXT("Error creating the system tray icon. Quitting."));
    }

    LOG("Notification icon successfully created.");
}

static void RemoveNotificationIcon(HWND windowHandle)
{
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = windowHandle;
    nid.uFlags = NIF_ICON;
    nid.uID = TRAY_ICON_UUID;
    nid.hIcon = cb.programIcon;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

static void ShowContextMenu(HWND windowHandle, POINT point)
{
    if (cb.inDialog)
    {
        return;
    }

    pthread_mutex_lock(&glbl.lock);
    char isDisabled = glbl.isDisabled;
    pthread_mutex_unlock(&glbl.lock);

    if (isDisabled)
    {
        ShowDisabledContextMenu(windowHandle, point);
    }
    else
    {
        ShowEnabledContextMenu(windowHandle, point);
    }
}

static void ShowEnabledContextMenu(HWND windowHandle, POINT point)
{
    HMENU hMenu = LoadMenu(cb.instanceHandle, MAKEINTRESOURCE(ENABLED_CONTEXT_MENU_ID));

    if (!hMenu)
    {
        LOG_ERROR("Failed to load enable context menu");
        return;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
    {
        LOG_ERROR("Failed to obtain enable context submenu");
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

static void ShowDisabledContextMenu(HWND windowHandle, POINT point)
{
    HMENU hMenu = LoadMenu(cb.instanceHandle, MAKEINTRESOURCE(DISABLED_CONTEXT_MENU_ID));

    if (!hMenu)
    {
        LOG_ERROR("Failed to load disable context menu");
        return;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);

    if (!hSubMenu)
    {
        LOG_ERROR("Failed to obtain disable context submenu");
        goto cleanup;
    }

    // Our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away.
    SetForegroundWindow(windowHandle);

    // If the timer's duration is 0 then it's disabled indefinitely and we don't need to write until when it's disabled.
    if (cb.currentTimerDuration > 0)
    {
        // Writing the text. End result should look like: "Enable AlwaysShadow (disabled until 18:32)"
        TCHAR txt[256];
        _stprintf_s(txt, sizeof(txt) / sizeof(*txt), TEXT("Enable ") TCS_FMT TEXT(" (disabled until %u:%02u)"),
            PROGRAM_NAME, cb.timerEndTime.wHour, cb.timerEndTime.wMinute);

        MENUITEMINFO mi = {0};
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
    LOG("Opened disable context menu");

cleanup:
    DestroyMenu(hMenu);
}

// IMPORTANT: This function cannot use LOG because it is called before logging is initialized.
static void Panic(LPTSTR msg)
{
    MessageBox(cb.mainWindowHandle, msg == NULL ? TEXT("An unidentified error has occured. Quitting.") : msg,
        PROGRAM_NAME TEXT(" - Error"), MB_OK | MB_ICONERROR);
    exit(1);
}

static void Warn(LPTSTR msg)
{
    MessageBox(cb.mainWindowHandle, msg == NULL ? TEXT("An unidentified warning has warning has occured. This shouldn't happen.") : msg,
        PROGRAM_NAME TEXT(" - Warning"), MB_OK | MB_ICONWARNING);
}

#pragma endregion // MainWindow.

#pragma region Timer Picker Dialog

static INT_PTR TimePickerProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            {
                // Add items to lists.
                FillListbox(hDlg, HOURS_LISTBOX_ID, hours, _countof(hours));
                FillListbox(hDlg, MINUTES_LISTBOX_ID, minutes, _countof(minutes));
                FillListbox(hDlg, SECONDS_LISTBOX_ID, seconds, _countof(seconds));
                SendMessage(hDlg, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)cb.programIcon);
                return TRUE;               
            }
        case WM_CLOSE:
            EndDialog(hDlg, 0);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case APPLY_PICKER_BTN_ID:
                    {
                        int hoursSel = GetSelection(hDlg, HOURS_LISTBOX_ID);
                        int minutesSel = GetSelection(hDlg, MINUTES_LISTBOX_ID);
                        int secondsSel = GetSelection(hDlg, SECONDS_LISTBOX_ID);
                        UINT duration = hours[hoursSel].amount * MILLIS_PER_HOUR +
                                        minutes[minutesSel].amount * MILLIS_PER_MINUTE +
                                        seconds[secondsSel].amount * MILLIS_PER_SECOND;

                        EndDialog(hDlg, duration);
                        return TRUE;
                    }
                case CANCEL_PICKER_BTN_ID:
                    EndDialog(hDlg, 0);
                    return TRUE;
            }

            return TRUE;
    }

    return FALSE;
}

static void FillListbox(HWND dialog, int id, const TimeOption *items, size_t nitems)
{
    HWND listbox = GetDlgItem(dialog, id);

    for (int i = 0; i < nitems; i++)
    { 
        int pos = SendMessage(listbox, LB_ADDSTRING, 0, (LPARAM)items[i].text);

        // Set the array index of the item so we can retrieve it later.
        SendMessage(listbox, LB_SETITEMDATA, pos, (LPARAM)i); 
    }
}

static int GetSelection(HWND dialog, int id)
{
    HWND listbox = GetDlgItem(dialog, id);
    int selection = SendMessage(listbox, LB_GETCURSEL, 0, 0);

    if (selection == LB_ERR)
    {
        LOG_WARN("Got LB_ERR for %#x, error code %#lx (could just mean the user didn't select seconds/minutes/hours)", id, GetLastError());
        selection = 0;
    }

    return selection;
}

#pragma endregion // Time Picker Dialog
