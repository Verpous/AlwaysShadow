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

#pragma region Macros

// The name of this program.
#define SCANCODE_LALT 0x38 
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_F10 0x44

#define TEMP_PATH_SUFFIX L"9343b833-e7af-42ea-8a61-31bc41eefe2b\\Sha*.tmp"

#pragma endregion // Macros.

// TODO: Get shadowplay toggle shortcut from registry (this may be harder to do, maybe just give up or let the user manually assign the same shortcut.)
// TODO: Investigate potential conflicts with Netflix.
// TODO: Force single instance.

void* FixerLoop(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    WCHAR tempFilesPath[MAX_PATH];
	DWORD bufsz = sizeof(tempFilesPath);
	LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), TEXT("TempFilePath"), RRF_RT_ANY, NULL, (PVOID)tempFilesPath, &bufsz);

    if (ret != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to fetch temp file path with error code 0x%lX\n", ret);
        exit(1);
    }
    
    if (wcscat_s(tempFilesPath, _countof(tempFilesPath), TEMP_PATH_SUFFIX) != 0)
    {
        fprintf(stderr, "Failed to append file path suffix.\n");
        exit(1);
    }

    for (;;)
    {
        sleep(3);

        if (!isDisabled)
        {
            if (!IsInstantReplayOn(tempFilesPath))
            {
                ToggleInstantReplay();
            }
        }
    }

    return 0;
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

INPUT CreateInput(WORD scancode, char isDown)
{
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scancode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (isDown ? 0 : KEYEVENTF_KEYUP);
    return input;
}

void ToggleInstantReplay()
{
    // Simulating Alt+Shift+F10 which toggles Instant Replay.
    INPUT inputs[6] = {};
    ZeroMemory(inputs, sizeof(inputs));

    inputs[0] = CreateInput(SCANCODE_LALT, TRUE);
    inputs[1] = CreateInput(SCANCODE_LSHIFT, TRUE);
    inputs[2] = CreateInput(SCANCODE_F10, TRUE);

    inputs[3] = CreateInput(SCANCODE_LALT, FALSE);
    inputs[4] = CreateInput(SCANCODE_LSHIFT, FALSE);
    inputs[5] = CreateInput(SCANCODE_F10, FALSE);

    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}