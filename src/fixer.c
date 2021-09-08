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

#include "defines.h"
#include <stdio.h>      // For printing errors and such.
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.
#include <unistd.h>     // For sleep.
#include <wbemidl.h>    // For getting the command line of running processes.
#include <oleauto.h>    // For working with BSTRs.

#pragma region Macros

// This came with the whitelisting function which I dare not touch.
#define _WIN32_DCOM

#pragma endregion // Macros.

volatile TCHAR* errorMsg = NULL;

static size_t ninputs = 0;
static INPUT* inputs = NULL;

static size_t nprocs = 0;
static BSTR* procCmds = NULL;

static char comInitialized = FALSE;
static IWbemLocator* wbemLocator = NULL;
static IWbemServices* wbemServices = NULL;

void* FixerLoop(void* arg)
{
    // Making thread cancellable.
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    WCHAR tempFilesPath[MAX_PATH];
	FetchTempFilesPath(tempFilesPath, sizeof(tempFilesPath), _countof(tempFilesPath));
    inputs = FetchToggleShortcut(&ninputs);
    procCmds = FetchWhitelist(&nprocs);

    if (nprocs > 0)
    {
        InitializeWmi();
    }

    for (;;)
    {
        sleep(10);

        if (isDisabled)
        {
            continue;
        }

        if (!IsInstantReplayOn(tempFilesPath) && !WhitelistedProcessIsRunning(procCmds, nprocs))
        {
            ToggleInstantReplay(inputs, ninputs);
        }
    }
    
    return 0;
}

void ThreadError(TCHAR* msg)
{
    ReleaseResources(NULL);
    
    size_t len = _tcslen(msg);
    errorMsg = malloc((len + 1) * sizeof(*errorMsg));

    if (errorMsg != NULL && _tcscpy_s(errorMsg, len + 1, msg) != 0)
    {
        free(errorMsg);
        errorMsg = NULL;
    }
    
    fixerDied = TRUE;
    pthread_exit(NULL);
}

void ReleaseResources(void* arg)
{
    // This program does a sloppy job of cleanup.
    // When this thread has an error, we clean up its resources but not the main thread's.
    // When the main thread has an error, we don't clean up shit.
    // When the program exits normally, we clean up the main thread's shit, but not this thread's.
    // But you know what? Fuck it.
    if (wbemServices != NULL) wbemServices->lpVtbl->Release(wbemServices);
    if (wbemLocator != NULL) wbemLocator->lpVtbl->Release(wbemLocator);
    if (comInitialized) CoUninitialize();

    for (size_t i = 0; i < nprocs; i++)
    {
        SysFreeString(procCmds[i]);
    }

    free(procCmds);
    free(inputs);
}

#pragma region Checking-Active

// For some reason if we try to compute bufsz/sizeof(*buffer) instead of countof, wcscat fails.
void FetchTempFilesPath(WCHAR* buffer, DWORD bufsz, rsize_t countof)
{
	LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), TEXT("TempFilePath"), RRF_RT_ANY, NULL, (PVOID)buffer, &bufsz);

    if (ret != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to fetch temp file path with error code 0x%lX\n", ret);
        ThreadError(TEXT("Failed to detect settings for identifying if Instant Replay is on. Quitting."));
    }
    
    if (wcscat_s(buffer, countof, L"9343b833-e7af-42ea-8a61-31bc41eefe2b\\Sha*.tmp") != 0)
    {
        fprintf(stderr, "Failed to append file path suffix.\n");
        ThreadError(TEXT("Failed to detect settings for identifying if Instant Replay is on. Quitting."));
    }
}

char IsInstantReplayOn(WCHAR* tempFilesPath)
{
    // We detect if Instant Replay is on by checking if its temp files exist.
    WIN32_FIND_DATA fileData;
    HANDLE fileHandle = FindFirstFile(tempFilesPath, &fileData);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    
    FindClose(fileHandle);
    return TRUE;
}

#pragma endregion // Checking-Active

#pragma region Toggling-Active

INPUT* FetchToggleShortcut(size_t* ninputs)
{
    INPUT* shortcut;

    DWORD hkeyCount;
    DWORD bufsz = sizeof(hkeyCount);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), TEXT("IRToggleHKeyCount"), RRF_RT_ANY, NULL, (PVOID)&hkeyCount, &bufsz);

    // Defaulting to Alt+Shift+F10.
    if (ret != ERROR_SUCCESS)
    {
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
        *ninputs = hkeyCount * 2;
        size_t halfinputs = (*ninputs) / 2;
        shortcut = calloc(*ninputs, sizeof(INPUT));

        // Reading each character of the shortcut from its own registry entry.
        for (int i = 0; i < halfinputs; i++)
        {
            // Creating string of registry name (Should be IRToggleHKey0, IRToggleHKey1, etc.).
            TCHAR valueName[256];
            if (_sntprintf_s(valueName, _countof(valueName), _TRUNCATE, TEXT("IRToggleHKey%d"), i) == -1)
            {
                fprintf(stderr, "This shortcut must be hella long because I can't fit the number in this buffer!\n");
                ThreadError(TEXT("Failed to detect settings for being able to turn on Instant Replay. Quitting."));
            }

            // Reading the registry entry.
            DWORD vkey;
            bufsz = sizeof(vkey);
            ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), valueName, RRF_RT_ANY, NULL, (PVOID)&vkey, &bufsz);

            if (ret != ERROR_SUCCESS)
            {
                fprintf(stderr, "Failed to read hotkey %d with error code 0x%lX\n", i, ret);
                ThreadError(TEXT("Failed to detect settings for being able to turn on Instant Replay. Quitting."));
            }

            CreateInput(shortcut + i, *((WORD*)(&vkey)), TRUE);
            CreateInput(shortcut + halfinputs + i, *((WORD*)(&vkey)), FALSE);
        }
    }

    return shortcut;
}

void CreateInput(INPUT* input, WORD vkey, char isDown)
{
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = vkey;
    input->ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
}

void ToggleInstantReplay(INPUT* inputs, size_t ninputs)
{
    SendInput((UINT)ninputs, inputs, sizeof(INPUT));
}

# pragma endregion // Toggling-Active

# pragma region Whitelisting

void InitializeWmi()
{
    const TCHAR* error = TEXT("Failed to set up ability to detect if whitelisted programs are running. Quitting.");

    // Initializate the Windows security.
    if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)))
    {
        fprintf(stderr, "CoInitializeEx failed.\n");
        ThreadError(error);
    }

    comInitialized = TRUE;

    if (FAILED(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL)))
    {
        fprintf(stderr, "CoInitializeSecurity failed.\n");
        ThreadError(error);
    }

    if (FAILED(CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID*)&wbemLocator)))
    {
        fprintf(stderr, "CoCreateInstance failed.\n");
        ThreadError(error);
    }

    if (FAILED(wbemLocator->lpVtbl->ConnectServer(wbemLocator, L"ROOT\\CIMV2", NULL, NULL, NULL, 0, NULL, NULL, &wbemServices)))
    {
        fprintf(stderr, "ConnectServer failed.\n");
        ThreadError(error);
    }
}

BSTR* FetchWhitelist(size_t* nprocs)
{
    BSTR* procCmds = NULL;
    FILE* whitelist;

    if (_tfopen_s(&whitelist, TEXT("Whitelist.txt"), TEXT("r")) != 0)
    {
        // Defaulting to filtering just Netflix.
        procCmds = malloc(sizeof(BSTR));
        procCmds[0] = SysAllocString(L"\"C:\\WINDOWS\\system32\\wwahost.exe\" -ServerName:Netflix.App.wwa");
        *nprocs = 1;
        fprintf(stderr, "Couldn't open whitelist. Defaulting to netflix.\n");
        return procCmds;
    }

    *nprocs = 0;
    char buffer[1 << 13];
    wchar_t wbuffer[sizeof(buffer)];
    
    while (fgets(buffer, sizeof(buffer), whitelist) != NULL)
    {
        char* line = StripWhitespace(buffer);

        if (mbstowcs_s(NULL, wbuffer, _countof(wbuffer), line, _countof(wbuffer) - 1) != 0)
        {
            fprintf(stderr, "Failed to convert command line %s\n", line);
            continue;
        }
        else
        {
            fprintf(stderr, "Adding to the whitelist: %s\n", line);
        }

        procCmds = realloc(procCmds, ((*nprocs) + 1) * sizeof(BSTR));
        procCmds[*nprocs] = SysAllocString(wbuffer);
        (*nprocs)++;
    }

    fclose(whitelist);
    return procCmds;
}

// Thank god for StackOverflow for delivering this holy function : https://stackoverflow.com/a/9589788/12553917.
char WhitelistedProcessIsRunning(BSTR* procCmds, size_t nprocs)
{
    if (nprocs == 0)
    {
        return FALSE;
    }

    IEnumWbemClassObject* enumWbem = NULL;

    // Run the WQL Query.
    // TODO: instead of iterating over all processes, be informed when a process is created or destroyed. Some interesting reading material on that:
    // https://stackoverflow.com/questions/3556048/how-to-detect-win32-process-creation-termination-in-c
    // https://www.codeproject.com/Articles/11985/Hooking-the-native-API-and-controlling-process-cre
    // https://docs.microsoft.com/en-us/windows/win32/wmisdk/example--receiving-event-notifications-through-wmi-?redirectedfrom=MSDN
    // If this is too difficult to do from C, I can write it in C++ and call it from C. It shouldn't be too hard to set up.
    if (FAILED(wbemServices->lpVtbl->ExecQuery(wbemServices, L"WQL",
        L"SELECT CommandLine FROM Win32_Process",
        WBEM_FLAG_FORWARD_ONLY, NULL, &enumWbem)))
    {
        return FALSE;
    }

    // Iterate over the enumerator.
    if (enumWbem != NULL)
    {
        IWbemClassObject* result = NULL;
        ULONG returnedCount = 0;

        while (enumWbem->lpVtbl->Next(enumWbem, WBEM_INFINITE, 1, &result, &returnedCount) == S_OK)
        {
            VARIANT commandLine;

            // Access the properties.
            if (FAILED(result->lpVtbl->Get(result, L"CommandLine", 0, &commandLine, 0, 0)))
            {
                result->lpVtbl->Release(result);
                continue;
            }

            if (commandLine.vt != VT_NULL)
            {
                wchar_t* line;
                size_t len = StripWhitespaceBSTR(commandLine.bstrVal, &line);

                for (size_t i = 0; i < nprocs; i++)
                {
                    if (wcsncmp(procCmds[i], line, max(SysStringLen(procCmds[i]), len)) == 0)
                    {
                        fprintf(stderr, "EQUALITY FOUND\n%ls\n%ls\n", procCmds[i], line);
                        result->lpVtbl->Release(result);
                        enumWbem->lpVtbl->Release(enumWbem);
                        return TRUE;
                    }
                }
            }

            result->lpVtbl->Release(result);
        }
    }

    // Release the resources.
    enumWbem->lpVtbl->Release(enumWbem);
    return FALSE;
}

char* StripWhitespace(char* str)
{
    while (isspace(*str))
    {
        str++;
    }

    size_t len = strlen(str);

    if (len > 0)
    {
        char* endstr = str + (len - 1);

        while (isspace(*endstr))
        {
            *(endstr--) = '\0';
        }
    }

    return str;
}

size_t StripWhitespaceBSTR(BSTR bstr, wchar_t** result)
{
    size_t len = SysStringLen(bstr);
    wchar_t* start = bstr;
    wchar_t* end = bstr + (len - 1);

    while (iswspace(*start))
    {
        start++;
    }

    size_t newlen = len - (start - bstr);

    if (len > 0)
    {
        while (iswspace(*end))
        {
            newlen--;
            end--;
        }
    }

    *result = start;
    return newlen;
}

#pragma endregion // Whitelisting.