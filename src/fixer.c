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

#include "defines.h"
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.
#include <unistd.h>     // For sleep.
#include <wbemidl.h>    // For getting the command line of running processes.
#include <oleauto.h>    // For working with BSTRs.
#include <regex.h>      // For parsing the whitelist.

// This came with the whitelisting function which I dare not touch.
#define _WIN32_DCOM

typedef struct
{
    BSTR command;
    char isSubstring;
    char isExclusive;
} WhitelistEntry;

typedef struct
{
    size_t ninputs;
    INPUT *inputs;

    size_t nwhitelist;
    WhitelistEntry *whitelist;
    char isExclusiveExists;

    char comInitialized;
    IWbemLocator *wbemLocator;
    IWbemServices *wbemServices;
} FixerCb;

static void Panic(LPTSTR msg);
static void Warn(LPTSTR msg);
static void ReleaseResources(char freeWmi);
static void LoadResources(char loadWmi);
static char IsInstantReplayOn();

static INPUT *FetchToggleShortcut(size_t *ninputs);
static void CreateInput(INPUT *input, WORD vkey, char isDown);
static void ToggleInstantReplay();

static void InitializeWmi();
static WhitelistEntry *FetchWhitelist(LPTSTR filename, size_t *nwhitelist);
static char IsExclusiveExists(WhitelistEntry *whitelist, size_t nwhitelist);
static void PollRunningProcesses(WhitelistEntry *whitelist, size_t nwhitelist, char *isWhitelistedRunning, char *isExclusiveRunning);
static wchar_t *StripLeadingTrailingWhitespaceWide(wchar_t *str);
static char IsMatchingCommandLine(BSTR command, WhitelistEntry *entry);

static FixerCb cb = {0};

// TODO: See about not triggering language switch (alt+shift) when pressing alt+shift+f10.
// TODO: See about detecting that in-game overlay is off and notifying the user to turn it on.
// TODO: See about detecting apps that are running but suspended.

void *FixerLoop(void *arg)
{
    // Making thread cancellable.
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // Loading whitelist, shortcut, wmi, everything.
    LoadResources(TRUE);

    for (;;)
    {
        sleep(10);

        pthread_mutex_lock(&glbl.lock);
        char isRefresh = glbl.isRefresh;
        char isDisabled = glbl.isDisabled;
        glbl.isRefresh = FALSE;
        pthread_mutex_unlock(&glbl.lock);

        if (isRefresh)
        {
            LOG("Received refresh signal. Refreshing.");
            ReleaseResources(FALSE);
            LoadResources(FALSE);
        }

        if (isDisabled) continue;

        char isInstantReplayOn = IsInstantReplayOn();

        // When these conditions are met there is no reason to waste cpu time polling running processes.
        if (!cb.isExclusiveExists && isInstantReplayOn) continue;

        char isWhitelistedRunning, isExclusiveRunning;
        PollRunningProcesses(cb.whitelist, cb.nwhitelist, &isWhitelistedRunning, &isExclusiveRunning);

        // Whitelist disables AlwaysShadow, taking precedence over Exclusives list.
        if (isWhitelistedRunning) continue;

        if ((!isInstantReplayOn && (!cb.isExclusiveExists || isExclusiveRunning)) || // Conditions for toggling ON.
            (isInstantReplayOn && cb.isExclusiveExists && !isExclusiveRunning)) // Conditions for toggling OFF.
        {
            LOG("Toggling because: isInstantReplayOn %d, isExclusiveExists %d, isExclusiveRunning %d", isInstantReplayOn, cb.isExclusiveExists, isExclusiveRunning);
            ToggleInstantReplay();
        }
    }
    
    return 0;
}

static void Panic(LPTSTR msg)
{
    ReleaseResources(TRUE);

    pthread_mutex_lock(&glbl.lock);
    _tcscpy_s(glbl.errorMsg, _countof(glbl.errorMsg), msg);
    glbl.fixerDied = TRUE;
    pthread_mutex_unlock(&glbl.lock);
    pthread_exit(NULL);
}

static void Warn(LPTSTR msg)
{
    for (;;)
    {
        pthread_mutex_lock(&glbl.lock);

        // If previous warning hasn't been displayed yet, release lock and try again later.
        if (glbl.issueWarning)
        {
            pthread_mutex_unlock(&glbl.lock);
            sleep(1);
            continue;
        }

        _tcscpy_s(glbl.warningMsg, _countof(glbl.warningMsg), msg);
        glbl.issueWarning = TRUE;
        pthread_mutex_unlock(&glbl.lock);
        break;
    }
}

static void ReleaseResources(char freeWmi)
{
    // This program does a sloppy job of cleanup.
    // When this thread has an error, we clean up its resources but not the main thread's.
    // When the main thread has an error, we don't clean up shit.
    // When the program exits normally, we clean up the main thread's shit, but not this thread's.
    // But you know what? Fuck it.
    if (freeWmi)
    {
        if (cb.wbemServices != NULL) cb.wbemServices->lpVtbl->Release(cb.wbemServices);
        if (cb.wbemLocator != NULL) cb.wbemLocator->lpVtbl->Release(cb.wbemLocator);
        if (cb.comInitialized) CoUninitialize();

        cb.wbemServices = NULL;
        cb.wbemLocator = NULL;
        cb.comInitialized = FALSE;
    }

    for (size_t i = 0; i < cb.nwhitelist; i++) SysFreeString(cb.whitelist[i].command);
    free(cb.whitelist);
    free(cb.inputs);

    cb.whitelist = NULL;
    cb.inputs = NULL;
    cb.nwhitelist = 0;
    cb.ninputs = 0;
}

static void LoadResources(char loadWmi)
{
    if (loadWmi) InitializeWmi();
    cb.inputs = FetchToggleShortcut(&cb.ninputs);
    cb.whitelist = FetchWhitelist(TEXT("Whitelist.txt"), &cb.nwhitelist);
    cb.isExclusiveExists = IsExclusiveExists(cb.whitelist, cb.nwhitelist);
}

#pragma region Checking-Active

static char IsInstantReplayOn()
{
    // There's a registry key which will tell us if it's on.
    DWORD isActive;
    DWORD bufsz = sizeof(isActive);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"),
        TEXT("{1B1D3DAA-601D-49E5-8508-81736CA28C6D}"), RRF_RT_ANY, NULL, (PVOID)&isActive, &bufsz);

    if (ret != ERROR_SUCCESS)
    {
        LOG_WARN("Failed to read registry key check if Instant Replay is on with error code %#lx", ret);
        return TRUE;
    }

    // Technically isActive is already 0/1 but since it's a DWORD and we want to return a char, !! will do it safely.
    return !!isActive;
}

#pragma endregion // Checking-Active

#pragma region Toggling-Active

static INPUT *FetchToggleShortcut(size_t *ninputs)
{
    INPUT *shortcut;

    DWORD hkeyCount;
    DWORD bufsz = sizeof(hkeyCount);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"),
        TEXT("IRToggleHKeyCount"), RRF_RT_ANY, NULL, (PVOID)&hkeyCount, &bufsz);

    // Defaulting to Alt+Shift+F10.
    if (ret != ERROR_SUCCESS)
    {
        LOG_WARN("Resorting to default toggle shortcut.");

        *ninputs = 6;
        size_t halfinputs = (*ninputs) / 2;
        shortcut = calloc(*ninputs, sizeof(INPUT));
        
        CreateInput(shortcut + 0, VK_MENU, TRUE);
        CreateInput(shortcut + 1, VK_SHIFT, TRUE);
        CreateInput(shortcut + 2, VK_F10, TRUE);

        CreateInput(shortcut + halfinputs + 0, VK_MENU, FALSE);
        CreateInput(shortcut + halfinputs + 1, VK_SHIFT, FALSE);
        CreateInput(shortcut + halfinputs + 2, VK_F10, FALSE);
    }
    else // Reading from registry.
    {
        LOG("Shortcut length: %ld", hkeyCount);

        *ninputs = hkeyCount * 2;
        size_t halfinputs = (*ninputs) / 2;
        shortcut = calloc(*ninputs, sizeof(INPUT));

        // Reading each character of the shortcut from its own registry entry.
        for (int i = 0; i < halfinputs; i++)
        {
            // Creating string of registry name (Should be IRToggleHKey0, IRToggleHKey1, etc.).
            TCHAR valueName[1 << 8];
            if (_sntprintf_s(valueName, _countof(valueName), _TRUNCATE, TEXT("IRToggleHKey%d"), i) == -1)
            {
                LOG_ERROR("Error fitting registry key %d in this buffer!", i);
                PANIC(TEXT("Failed to prepare for reading the toggle shortcut. Quitting."));
            }

            // Reading the registry entry.
            DWORD vkey;
            bufsz = sizeof(vkey);
            ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), valueName, RRF_RT_ANY, NULL, (PVOID)&vkey, &bufsz);

            if (ret != ERROR_SUCCESS)
            {
                LOG_ERROR("Failed to read hotkey %d with error code %#lx", i, ret);
                PANIC(TEXT("Failed to read toggle shortcut key %d with error code %#lx. Quitting."), i, ret);
            }

            LOG("Adding vkey %#lx to shortcut.", vkey);
            CreateInput(shortcut + i, *((WORD *)(&vkey)), TRUE);
            CreateInput(shortcut + halfinputs + i, *((WORD *)(&vkey)), FALSE);
        }
    }

    return shortcut;
}

static void CreateInput(INPUT *input, WORD vkey, char isDown)
{
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = vkey;
    input->ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
}

static void ToggleInstantReplay()
{
    SendInput((UINT)cb.ninputs, cb.inputs, sizeof(INPUT));
}

# pragma endregion // Toggling-Active

# pragma region Whitelisting

static void InitializeWmi()
{
    const TCHAR *error = TEXT("Failed to initialize WMI: ") TCS_FMT TEXT(" returned %d. Quitting.");
    HRESULT res;

    // Initializate the Windows security.
    if (FAILED(res = CoInitializeEx(0, COINIT_MULTITHREADED)))
    {
        LOG_ERROR("CoInitializeEx failed with res %ld.", res);
        PANIC(error, TEXT("CoInitializeEx"), res);
    }

    cb.comInitialized = TRUE;

    if (FAILED(res = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL)))
    {
        LOG_ERROR("CoInitializeSecurity failed with res %ld.", res);
        PANIC(error, TEXT("CoInitializeSecurity"), res);
    }

    if (FAILED(res = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID *)&cb.wbemLocator)))
    {
        LOG_ERROR("CoCreateInstance failed with res %ld.", res);
        PANIC(error, TEXT("CoCreateInstance"), res);
    }

    if (FAILED(res = cb.wbemLocator->lpVtbl->ConnectServer(cb.wbemLocator, L"ROOT\\CIMV2", NULL, NULL, NULL, 0, NULL, NULL, &cb.wbemServices)))
    {
        LOG_ERROR("ConnectServer failed with res %ld.", res);
        PANIC(error, TEXT("ConnectServer"), res);
    }
}

// fmt must end with a %s where the error string goes.
#define LOG_REGERROR(errcode, compiled, fmt, ...)                                   \
    do {                                                                            \
        char bufLogRegerror[MSG_LEN];                                               \
        regerror((errcode), (compiled), bufLogRegerror, sizeof(bufLogRegerror));    \
        LOG_WARN(fmt, ##__VA_ARGS__, bufLogRegerror);                               \
    } while (FALSE)

static WhitelistEntry *FetchWhitelist(LPTSTR filename, size_t *nwhitelist)
{
    FILE *file = NULL;
    WhitelistEntry *whitelist = NULL;
    // Have to initialize these to *something* that isn't REG_OK.
    int emptylineCompRes = REG_BADPAT;
    int modlineCompRes = REG_BADPAT;
    int normlineCompRes = REG_BADPAT;
    int res;
    *nwhitelist = 0;

    if ((res = _tfopen_s(&file, filename, TEXT("r"))) != 0)
    {
        LOG_WARN("Couldn't open whitelist with error %s.", strerror(res));
        if (res != ENOENT) WARN(NULL, TEXT("Failed to open whitelist: ") TCS_FMT TEXT(". Fix the problem then refresh."), _tcserror(res));
        file = NULL; // Just to be sure.
        goto bad;
    }

    regex_t emptylineRegex;
    regex_t modlineRegex;
    regex_t normlineRegex;
    emptylineCompRes = regcomp(&emptylineRegex, "^\\s*$", REG_EXTENDED | REG_NOSUB);
    modlineCompRes = regcomp(&modlineRegex, "^\\s*\\?\\?(\\S*)\\s*((\\s*\\S*)*)\\s*$", REG_EXTENDED);
    normlineCompRes = regcomp(&normlineRegex, "^\\s*(\\S+(\\s*\\S+)*)\\s*$", REG_EXTENDED);

    if (emptylineCompRes != REG_OK || modlineCompRes != REG_OK || normlineCompRes != REG_OK)
    {
        LOG_REGERROR(emptylineCompRes, &emptylineRegex, "emptylineRegex compilation result: %s.");
        LOG_REGERROR(modlineCompRes, &modlineRegex, "modlineRegex compilation result: %s.");
        LOG_REGERROR(normlineCompRes, &normlineRegex, "normlineRegex compilation result: %s.");
        WARN(NULL, TEXT("Failed to load whitelist due to an internal problem. You can retry by hitting refresh."));
        goto bad;
    }

    char buffer[1 << 13];
    wchar_t wbuffer[_countof(buffer)];
    int linenum = 0;

    // Iterate over the file line by line.
    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        linenum++;

        // fgets writes a newline to the buffer and this is the easiest way I was able to shake it off.
        for (int i = strlen(buffer) - 1; i >= 0 && (buffer[i] == '\n' || buffer[i] == '\r'); i--) buffer[i] = '\0';
        LOG("Checking whitelist line: %d line length: %lld contents: '%s'.", linenum, strlen(buffer), buffer);

        // Check for empty lines, skip them.
        res = regexec(&emptylineRegex, buffer, 0, NULL, 0);

        if (res == REG_OK)
        {
            LOG("Skipping line %d because it is empty.", linenum);
            continue;
        }

        if (res != REG_NOMATCH)
        {
            LOG_REGERROR(res, &emptylineRegex, "Line %d emptylineRegex exec result: %s.", linenum);
            WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
            goto bad;
        }

        // We'll write the entry to this variable then create a dynamically allocated copy.
        // The reason is so if we get an error in the middle we don't have to worry about freeing that allocation.
        WhitelistEntry entry = {0};

        // Plenty of array size just to be safe.
        regmatch_t matches[16];
        regmatch_t *commandMatch = NULL;
        res = regexec(&modlineRegex, buffer, _countof(matches), matches, 0);

        if (res == REG_OK)
        {
            regmatch_t *flagsMatch = &matches[1];
            commandMatch = &matches[2];

            if (flagsMatch->rm_so == flagsMatch->rm_eo)
            {
                LOG_WARN("Line %d in the whitelist starts with ?? but has no flags.", linenum);
                WARN(NULL, TEXT("Invalid whitelist line: %d - line starts with ?? but has no flags. Fix the problem then refresh."), linenum);
                goto bad;
            }

            char isComment = FALSE;

            for (char *c = &buffer[flagsMatch->rm_so]; c != &buffer[flagsMatch->rm_eo]; c++)
            {
                switch (*c)
                {
                    case 'S':
                        entry.isSubstring = TRUE;
                        break;
                    case 'E':
                        entry.isExclusive = TRUE;
                        break;
                    case 'I':
                        // Keep iterating over flag characters even if this is a comment.
                        isComment = TRUE;
                        break;
                    default:
                        LOG_WARN("Invalid flag character: %c in line: %d.", *c, linenum);
                        WARN(NULL, TEXT("Invalid whitelist line: %d - flag character: '%c' is unrecognized. Fix the problem then refresh."), linenum, *c);
                        goto bad;
                }
            }

            // If this is a comment line, skip it.
            if (isComment)
            {
                LOG("Skipping line %d because it is a comment.", linenum);
                continue;
            }

            if (commandMatch->rm_so == commandMatch->rm_eo)
            {
                LOG_WARN("Line %d in the whitelist has flags but no command.", linenum);
                WARN(NULL, TEXT("Invalid whitelist line: %d - command is missing. Fix the problem then refresh."), linenum);
                goto bad;
            }
        }
        else if (res != REG_NOMATCH)
        {
            LOG_REGERROR(res, &modlineRegex, "Line %d modlineRegex exec result: %s.", linenum);
            WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
            goto bad;
        }
        else // res == REG_NOMATCH. We'll try normlineRegex.
        {
            res = regexec(&normlineRegex, buffer, _countof(matches), matches, 0);
            commandMatch = &matches[1];

            if (res != REG_OK)
            {
                LOG_REGERROR(res, &modlineRegex, "Line %d normlineRegex exec result: %s.", linenum);
                WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
                goto bad;
            }
        }

        // Converting command to wchar.
        if ((res = mbstowcs_s(NULL, wbuffer, _countof(wbuffer), &buffer[commandMatch->rm_so], commandMatch->rm_eo - commandMatch->rm_so)) != 0)
        {
            LOG_WARN("Received error '%s' when trying to convert line %d command %.*s.",
                strerror(res), linenum, commandMatch->rm_eo - commandMatch->rm_so, &buffer[commandMatch->rm_so]);
            WARN(NULL, TEXT("Failed to load the whitelist due to a problem at line: %d. It may contain unsupported characters. Fix the problem then refresh."), linenum);
            goto bad;
        }

        entry.command = SysAllocString(wbuffer);

        // Allocate new whitelist entry.
        whitelist = realloc(whitelist, ((*nwhitelist) + 1) * sizeof(*whitelist));
        whitelist[*nwhitelist] = entry;
        (*nwhitelist)++;

        LOG("Added to the whitelist: line: %d, isSubstring: %d, isExclusive: %d, command length: %lld command: '%ls'.",
            linenum, entry.isSubstring, entry.isExclusive, wcslen(wbuffer), wbuffer);
    }

    // Skip bad.
    goto ret;

bad:
    // Safe to pass NULL to SysFreeString (also to free).
    for (size_t i = 0; whitelist != NULL && i < *nwhitelist; i++) SysFreeString(whitelist[i].command);
    free(whitelist);
    *nwhitelist = 0;
    whitelist = NULL;
ret:
    if (emptylineCompRes == REG_OK) regfree(&emptylineRegex);
    if (modlineCompRes == REG_OK) regfree(&modlineRegex);
    if (normlineCompRes == REG_OK) regfree(&normlineRegex);
    if (file != NULL) fclose(file);
    return whitelist;
}

static char IsExclusiveExists(WhitelistEntry *whitelist, size_t nwhitelist)
{
    for (int i = 0; i < cb.nwhitelist; i++)
    {
        if (whitelist[i].isExclusive) return TRUE;
    }

    return FALSE;
}

// Thank god for StackOverflow for delivering this holy function : https://stackoverflow.com/a/9589788/12553917.
static void PollRunningProcesses(WhitelistEntry *whitelist, size_t nwhitelist, char *isWhitelistedRunning, char *isExclusiveRunning)
{
    *isWhitelistedRunning = FALSE;
    *isExclusiveRunning = FALSE;
    if (nwhitelist == 0) return;

    // Run the WQL Query.
    // TODO: instead of iterating over all processes, be informed when a process is created or destroyed. Some interesting reading material on that:
    // https://stackoverflow.com/questions/3556048/how-to-detect-win32-process-creation-termination-in-c
    // https://www.codeproject.com/Articles/11985/Hooking-the-native-API-and-controlling-process-cre
    // https://docs.microsoft.com/en-us/windows/win32/wmisdk/example--receiving-event-notifications-through-wmi-?redirectedfrom=MSDN
    // If this is too difficult to do from C, I can write it in C++ and call it from C. It shouldn't be too hard to set up.
    IEnumWbemClassObject *enumWbem = NULL;
    if (FAILED(cb.wbemServices->lpVtbl->ExecQuery(cb.wbemServices, L"WQL", L"SELECT CommandLine FROM Win32_Process", WBEM_FLAG_FORWARD_ONLY, NULL, &enumWbem))) return;

    // Iterate over the enumerator.
    IWbemClassObject *result = NULL;
    ULONG returnedCount = 0;

    while (enumWbem->lpVtbl->Next(enumWbem, WBEM_INFINITE, 1, &result, &returnedCount) == S_OK)
    {
        VARIANT commandLine;

        // Access the properties.
        if (FAILED(result->lpVtbl->Get(result, L"CommandLine", 0, &commandLine, 0, 0))) goto next;
        if (commandLine.vt == VT_NULL) goto next;

        BSTR copy = SysAllocString(commandLine.bstrVal);
        BSTR whitespaceless = StripLeadingTrailingWhitespaceWide(copy);

        for (size_t i = 0; i < nwhitelist; i++)
        {
            WhitelistEntry *entry = &whitelist[i];

            if (IsMatchingCommandLine(whitespaceless, entry))
            {
                if (entry->isExclusive) *isExclusiveRunning = TRUE;
                else *isWhitelistedRunning = TRUE;

                LOG("Equality with %s process found:\n\tWhitelist: %ls\n\tProcess:   %ls",
                    entry->isExclusive ? "exclusive" : "whitelist",
                    entry->command, commandLine.bstrVal);
            }
        }

        SysFreeString(copy);

next:
        result->lpVtbl->Release(result);
    }

    // Release the resources.
    enumWbem->lpVtbl->Release(enumWbem);
}

static wchar_t *StripLeadingTrailingWhitespaceWide(wchar_t *str)
{
    while (iswspace(*str)) str++;
    size_t len = wcslen(str);

    if (len > 0)
    {
        wchar_t *endstr = str + (len - 1);
        while (iswspace(*endstr)) *(endstr--) = L'\0';
    }

    return str;
}

static char IsMatchingCommandLine(BSTR command, WhitelistEntry *entry)
{
    if (entry->isSubstring)
    {
        return wcsstr(command, entry->command) != NULL;
    }
    else
    {
        return wcscmp(command, entry->command) == 0;
    }
}

#pragma endregion // Whitelisting.
