/**
 * PROJECT:         Native Shell
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            main.c
 * DESCRIPTION:     This module handles the main command line interface,
 *                  interactive menu, and command parsing.
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#include "precomp.h"
#include "audio.h"

HANDLE hKeyboard;
HANDLE hHeap;
HANDLE hKey;

#define __APP_VER__ "0.15.0"
#if defined(_M_AMD64) || defined(_AMD64_)
#define __NCLI_VER__ __APP_VER__ " x64"
#else
#define __NCLI_VER__ __APP_VER__ " x86"
#endif

    WCHAR *helpstr[] =
        {
            {L"\n"
             L"badapple - Play Bad Apple animation\n"
             L"cd X     - Change directory to X    md X     - Make directory X\n"
             L"copy X Y - Copy file X to Y         poweroff - Power off PC\n"
             L"dir X    - Show directory contents  pwd      - Print working directory\n"
             L"del X    - Delete file X            reboot   - Reboot PC\n"
             L"devtree  - Dump device tree         shutdown - Shutdown PC\n"
             L"\x0000"},
            {L"exit     - Exit shell            sysinfo     - Dump system information\n"
             L"lm       - List modules          drawtext X  - Draw string X\n"
             L"lp       - List processes        move X Y    - Move file X to Y\n"
             L"testvid  - Test screen output    testarg X Y - Test argument parsing\n"
             L"\n"
             L"X: - change drive letter to X\n"
             L"If a command is not in the list, it is treated as an executable name\n"
             L"\n"
             L"\x0000"}};

/*++
 * @name RtlCliPlayBadApple
 *
 * Plays the Bad Apple!! animation using density-based ASCII art.
 * Optionally plays synchronized audio if the AC97 driver is loaded.
 *
 * Source: 160x120 1-bit packed frames at 30fps.
 * Output: 75x23 characters fitting the VGA text mode screen exactly.
 * Each character represents a 2x5 pixel block with density mapping.
 *
 * 11 density levels: 0-10 on-pixels map to ' .:--+**##@'
 *
 *--*/
#define OUT_W 75
#define OUT_H 23
#define BLOCK_W 2
#define BLOCK_H 5
static WCHAR FrameBuf[(OUT_W + 2) * OUT_H + 1];

VOID RtlCliPlayBadApple(VOID)
{
    VIDEO_CONTEXT video;
    UCHAR frame[VIDEO_FRAME_SIZE(160, 120)];
    WCHAR filePath[MAX_PATH];
    WCHAR currentDir[MAX_PATH];
    LARGE_INTEGER delay;
    ULONG cx, cy, px, py;
    UCHAR byteVal;
    ULONG totalFrames;
    WCHAR density[11];
    UNICODE_STRING frameStr;
    ULONG pos;
    int count;
    BOOLEAN audioActive;

    density[0]  = L' ';
    density[1]  = L'.';
    density[2]  = L'.';
    density[3]  = L':';
    density[4]  = L':';
    density[5]  = L'-';
    density[6]  = L'+';
    density[7]  = L'*';
    density[8]  = L'#';
    density[9]  = L'#';
    density[10] = L'@';

    RtlCliGetCurrentDirectory(currentDir);
    wcscpy(filePath, currentDir);
    if (filePath[wcslen(filePath) - 1] != L'\\')
        wcscat(filePath, L"\\");
    wcscat(filePath, L"badapple.dat");

    if (!VideoOpen(&video, filePath))
        return;

    delay.QuadPart = -((LONGLONG)10000000 / video.fps);

    /* Try to initialize audio */
    audioActive = FALSE;
    if (NT_SUCCESS(AudioInit()))
    {
        if (NT_SUCCESS(AudioConfigure(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_BITS_PER_SAMPLE)))
        {
            audioActive = TRUE;
            RtlCliDisplayString("Audio: initialized (%dHz %dbit)\n",
                                AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE);
        }
    }
    if (!audioActive)
    {
        RtlCliDisplayString("Audio: not available (driver not loaded)\n");
    }

    RtlCliDisplayString("Press Escape to stop...\n");
    NtDelayExecution(FALSE, &(LARGE_INTEGER){.QuadPart = -10000000}); /* 1 sec pause */

    GfxScrollClear();

    totalFrames = video.frameCount;

    while (VideoNextFrame(&video, frame, sizeof(frame)))
    {
        pos = 0;

        for (cy = 0; cy < OUT_H; cy++)
        {
            for (cx = 0; cx < OUT_W; cx++)
            {
                count = 0;

                for (py = 0; py < BLOCK_H; py++)
                {
                    for (px = 0; px < BLOCK_W; px++)
                    {
                        ULONG srcX = cx * BLOCK_W + px;
                        ULONG srcY = cy * BLOCK_H + py;
                        if (srcX >= 160 || srcY >= 120)
                            continue;
                        byteVal = frame[srcY * 20 + srcX / 8];
                        count += (byteVal >> (7 - (srcX % 8))) & 1;
                    }
                }

                FrameBuf[pos++] = density[count];
            }
            FrameBuf[pos++] = L'\n';
        }
        FrameBuf[pos] = UNICODE_NULL;

        frameStr.Buffer = FrameBuf;
        frameStr.Length = pos * sizeof(WCHAR);
        frameStr.MaximumLength = sizeof(FrameBuf);
        NtDisplayString(&frameStr);

        /* Play audio chunk for this frame if audio is active */
        if (audioActive)
        {
            ULONG audioOffset = video.currentFrame * AUDIO_FRAME_BYTES;
            ULONG totalAudioBytes = (ULONG)((ULONG_PTR)binary_badapple_audio_dat_size);

            if (audioOffset < totalAudioBytes)
            {
                ULONG chunkSize = AUDIO_FRAME_BYTES;
                ULONG remaining = totalAudioBytes - audioOffset;
                if (chunkSize > remaining)
                    chunkSize = remaining;

                AudioWrite((PVOID)(binary_badapple_audio_dat_start + audioOffset),
                           chunkSize);
            }
        }

        NtDelayExecution(FALSE, &delay);
    }

    if (audioActive)
    {
        AudioStop();
        AudioDeinit();
    }

    VideoClose(&video);
    GfxScrollClear();
    RtlCliDisplayString("Bad Apple finished. (%u frames)\n", totalFrames);
}

/*++
 * @name RtlCliShowAbout
 *
 * Displays the About screen with credits and project info.
 *
 *--*/
VOID RtlCliShowAbout(VOID)
{
    GfxScrollClear();
    RtlCliDisplayString("NativeShell v" __NCLI_VER__ "\n");
    RtlCliDisplayString("============================\n\n");
    RtlCliDisplayString("  A Windows Native Mode (subsystem 10)\n");
    RtlCliDisplayString("  shell that plays Bad Apple!! using\n");
    RtlCliDisplayString("  ASCII density art characters with\n");
    RtlCliDisplayString("  synchronized audio playback.\n\n");
    RtlCliDisplayString("  Created by Droid3002\n\n");
    RtlCliDisplayString("  Graphics : ASCII density (75x23)\n");
    RtlCliDisplayString("  Audio    : AC97 PCM driver\n");
    RtlCliDisplayString("  Video    : BAPL embedded format\n\n");
    RtlCliDisplayString("  Built for fun and to prove it\n");
    RtlCliDisplayString("  could be done.\n\n");
    RtlCliDisplayString("  https://github.com/Droid3002\n\n");
    RtlCliDisplayString("  Press any key to return...\n");
    RtlCliGetChar(hKeyboard);
}

VOID RtlClipProcessMessage(PCHAR Command);

/*++
 * @name RtlCliShellLoop
 *
 * The interactive command-line shell loop.
 * Typing 'exit' returns to the main menu.
 *
 *--*/
VOID RtlCliShellLoop(VOID)
{
    PCHAR Command;

    GfxScrollClear();
    RtlCliDisplayString("NativeShell command prompt. Type 'help' for commands.\n");
    RtlCliDisplayString("Type 'exit' to return to the menu.\n\n");

    while (TRUE)
    {
        /* Show prompt */
        {
            WCHAR CurrentDirectory[MAX_PATH];
            UNICODE_STRING DirString;
            RtlCliGetCurrentDirectory(CurrentDirectory);
            if (!RtlDosPathNameToNtPathName_U(CurrentDirectory, &DirString, NULL, NULL))
            {
                RtlCliDisplayString("%S> ", CurrentDirectory);
            }
            else
            {
                RtlCliPrintString(&DirString);
                RtlCliPutChar(L'>');
                RtlCliPutChar(L' ');
            }
        }

        Command = RtlCliGetLine(hKeyboard);
        RtlCliDisplayString("\n");

        if (*Command)
        {
            if (!_strnicmp(Command, "exit", 4))
                return;

            RtlClipProcessMessage(Command);
            RtlCliDisplayString("\n");
        }
    }
}

/*++
 * @name RtlCliShowMenu
 *
 * Displays the main interactive menu and handles user selection.
 *
 *   [1] Play Bad Apple
 *   [2] About
 *   [3] Shell
 *   [4] Exit
 *
 *--*/
VOID RtlCliShowMenu(VOID)
{
    CHAR key;

    while (TRUE)
    {
        GfxScrollClear();

        RtlCliDisplayString("NativeShell v" __NCLI_VER__ "\n");
        RtlCliDisplayString("============================\n\n");
        RtlCliDisplayString("    [1] Play Bad Apple\n");
        RtlCliDisplayString("    [2] About\n");
        RtlCliDisplayString("    [3] Shell\n");
        RtlCliDisplayString("    [4] Exit\n\n");
        RtlCliDisplayString("Select> ");

        key = RtlCliGetChar(hKeyboard);

        RtlCliDisplayString("%c\n\n", key);

        if (key == '1')
        {
            RtlCliPlayBadApple();
        }
        else if (key == '2')
        {
            RtlCliShowAbout();
        }
        else if (key == '3')
        {
            RtlCliShellLoop();
        }
        else if (key == '4')
        {
            GfxScrollClear();
            RtlCliDisplayString("Shutting down...\n");
            NtDelayExecution(FALSE, &(LARGE_INTEGER){.QuadPart = -20000000});
            DeinitHeapMemory(hHeap);
            NtTerminateProcess(NtCurrentProcess(), 0);
        }
    }
}

VOID RtlClipProcessMessage(PCHAR Command)
{
    WCHAR CurrentDirectory[MAX_PATH] = {0};
    UNICODE_STRING CurrentDirectoryString;
    CHAR CommandBuf[BUFFER_SIZE] = {0};
    UINT argc;
    CHAR **argv;

    strncpy(CommandBuf, Command, strnlen(Command, BUFFER_SIZE));

    argv = StringToArguments(&CommandBuf[0], &argc);

    if (0 == argc)
        return;

    if (!_strnicmp(argv[0], CMDSTR("exit")))
    {
        /* In menu mode, 'exit' is handled by the caller */
        return;
    }
    else if (!_strnicmp(argv[0], CMDSTR("badapple")))
    {
        RtlCliPlayBadApple();
    }
    else if (!_strnicmp(argv[0], CMDSTR("testarg")))
    {
        UINT i = 0;

        RtlCliDisplayString("Args: %d\n", argc);

        if (argc > 1)
        {
            for (i = 1; i < argc; i++)
            {
                if (NULL != argv[i])
                    RtlCliDisplayString("Arg %d: %s\n", i, argv[i]);
                else
                {
                    RtlCliDisplayString("Arg %d: NULL\n", i);
                    break;
                }
            }
        }
    }
    else if (!_strnicmp(argv[0], CMDSTR("help")))
    {
        RtlCliDisplayString("%S", helpstr[0]);
        RtlCliDisplayString("%S", helpstr[1]);
    }
    else if (!_strnicmp(argv[0], CMDSTR("lm")))
    {
        // List Modules (!lm)
        RtlCliListDrivers();
    }
    else if (!_strnicmp(argv[0], CMDSTR("lp")))
    {
        // List Processes (!lp)
        RtlCliListProcesses();
    }
    else if (!_strnicmp(argv[0], CMDSTR("sysinfo")))
    {
        // Dump System Information (sysinfo)
        RtlCliDumpSysInfo();
    }
    else if (!_strnicmp(argv[0], CMDSTR("cd")))
    {
        // Set the current directory
        RtlCliSetCurrentDirectory(&Command[3]);
    }
    else if (!_strnicmp(argv[0], CMDSTR("drawtext")))
    {
#if (NTDDI_VERSION >= NTDDI_WIN7)
        UNICODE_STRING us;
        ANSI_STRING as;
        RtlInitAnsiString(&as, &Command[9]);
        RtlAnsiStringToUnicodeString(&us, &as, TRUE);
        NtDrawText(&us);
        RtlFreeUnicodeString(&us);
#else
        RtlCliDisplayString("\nNot supported prior to Win7\n");
#endif
    }
    else if (!_strnicmp(argv[0], CMDSTR("pwd")))
    {
        // Get the current directory
        RtlCliGetCurrentDirectory(CurrentDirectory);

        // Display it
        RtlInitUnicodeString(&CurrentDirectoryString, CurrentDirectory);
        RtlCliPrintString(&CurrentDirectoryString);
    }
    else if (!_strnicmp(argv[0], CMDSTR("dir")))
    {
        WCHAR Dir[MAX_PATH];
        WCHAR ArgDir[MAX_PATH];
        RtlCliGetCurrentDirectory(Dir);
        if (argc > 1)
        {
            UNICODE_STRING us;
            ANSI_STRING as;
            RtlInitAnsiString(&as, argv[1]);
            RtlAnsiStringToUnicodeString(&us, &as, TRUE);

            AppendString(Dir, L"\\");
            AppendString(Dir, us.Buffer);

            RtlFreeUnicodeString(&us);
        }

        // List directory
        RtlCliListDirectory(Dir);
    }
    else if (!_strnicmp(argv[0], CMDSTR("devtree")))
    {
        // Dump hardware tree
        RtlCliListHardwareTree();
    }
    else if (!_strnicmp(argv[0], CMDSTR("shutdown")))
    {
        RtlCliShutdown();
    }
    else if (!_strnicmp(argv[0], CMDSTR("reboot")))
    {
        RtlCliReboot();
    }
    else if (!_strnicmp(argv[0], CMDSTR("poweroff")))
    {
        RtlCliPowerOff();
    }
    else if (!_strnicmp(argv[0], CMDSTR("testvid")))
    {
        UINT j;
        WCHAR i, w;
        UNICODE_STRING us;

        LARGE_INTEGER delay;
        memset(&delay, 0x00, sizeof(LARGE_INTEGER));
        delay.LowPart = 100000000;

        RtlInitUnicodeString(&us, L" ");

        // 75x23
        RtlCliDisplayString("\nVid mode is 75x23\n\nCharacter test:");

        j = 0;
        for (w = L'A'; w < 0xFFFF; w++)
        {
            j++;
            NtDelayExecution(FALSE, &delay);
            if (w != L'\n' && w != L'\r')
            {
                RtlCliPutChar(w);
            }
            else
            {
                RtlCliPutChar(L' ');
            }
            if (j > 70)
            {
                j = 0;
                RtlCliPutChar(L'\n');
            }
        }
    }
    else if (!_strnicmp(argv[0], CMDSTR("copy")))
    {
        // Copy file
        if (argc > 2)
        {
            WCHAR buf1[MAX_PATH] = {0};
            WCHAR buf2[MAX_PATH] = {0};
            GetFullPath(argv[1], buf1, FALSE);
            GetFullPath(argv[2], buf2, FALSE);
            RtlCliDisplayString("\nCopy %S to %S\n", buf1, buf2);
            if (FileExists(buf1))
            {
                if (!NtFileCopyFile(buf1, buf2))
                {
                    RtlCliDisplayString("Failed.\n");
                }
            }
            else
            {
                RtlCliDisplayString("File does not exist.\n");
            }
        }
        else
        {
            RtlCliDisplayString("Not enough arguments.\n");
        }
    }
    else if (!_strnicmp(argv[0], CMDSTR("move")))
    {
        // Move/rename file
        if (argc > 2)
        {
            WCHAR buf1[MAX_PATH] = {0};
            WCHAR buf2[MAX_PATH] = {0};
            GetFullPath(argv[1], buf1, FALSE);
            GetFullPath(argv[2], buf2, FALSE);
            RtlCliDisplayString("\nMove %S to %S\n", buf1, buf2);
            if (FileExists(buf1))
            {
                if (!NtFileMoveFile(buf1, buf2, FALSE))
                {
                    RtlCliDisplayString("Failed.\n");
                }
            }
            else
            {
                RtlCliDisplayString("File does not exist.\n");
            }
        }
        else
        {
            RtlCliDisplayString("Not enough arguments.\n");
        }
    }
    else if (!_strnicmp(argv[0], CMDSTR("del")))
    {
        // Delete file
        if (argc > 1)
        {
            WCHAR buf1[MAX_PATH] = {0};
            GetFullPath(argv[1], buf1, FALSE);
            if (FileExists(buf1))
            {
                RtlCliDisplayString("\nDelete %S\n", buf1);

                if (!NtFileDeleteFile(buf1))
                {
                    RtlCliDisplayString("Failed.\n");
                }
            }
            else
            {
                RtlCliDisplayString("File does not exist.\n");
            }
        }
        else
        {
            RtlCliDisplayString("Not enough arguments.\n");
        }
    }
    else if (!_strnicmp(argv[0], CMDSTR("md")))
    {
        // Make directory
        if (argc > 1)
        {
            WCHAR buf1[MAX_PATH] = {0};
            GetFullPath(argv[1], buf1, FALSE);

            RtlCliDisplayString("\nCreate directory %S\n", buf1);

            if (!NtFileCreateDirectory(buf1))
            {
                RtlCliDisplayString("Failed.\n");
            }
        }
        else
        {
            RtlCliDisplayString("Not enough arguments.\n");
        }
    }
    else if ((strlen(argv[0]) == 2) && (argv[0][1] == ':'))
    {
        // Change disk
        RtlCliSetCurrentDirectory(argv[0]);
        return;
    }
    else
    {
        // Unknown command, try to find an executable and run it.
        WCHAR filename[MAX_PATH] = {0};
        BOOL bExist = FALSE;

        GetFullPath(argv[0], filename, FALSE);

        bExist = FileExists(filename);
        if (!bExist)
        {
            wcscat(filename, L".exe");
            bExist = FileExists(filename);
        }

        if (bExist)
        {
            HANDLE hProcess;
            NTSTATUS status;
            ANSI_STRING as;
            UNICODE_STRING us;
            RtlInitAnsiString(&as, Command);
            RtlAnsiStringToUnicodeString(&us, &as, TRUE);

            NtClose(hKeyboard);

            status = CreateNativeProcess(filename, us.Buffer, &hProcess);
            if (NT_SUCCESS(status))
            {
                NtWaitForSingleObject(hProcess, FALSE, NULL);
            }
            else
            {
                RtlCliDisplayString("Failed to execute %s\n", Command);
            }
            RtlCliOpenInputDevice(&hKeyboard, KeyboardType);
            RtlFreeUnicodeString(&us);
        }
        else
        {
            RtlCliDisplayString("%s is not recognized as a command or an executable file name\n"
                                "\nType \"help\" for the list of commands.\n",
                                Command);
        }
    }
}

NTSTATUS
__cdecl shell_main(INT argc,
             PCHAR argv[],
             PCHAR envp[],
             ULONG DebugFlag OPTIONAL)
{
    NTSTATUS Status;

    hHeap = InitHeapMemory();
    hKey = NULL;

    // Setup keyboard input
    Status = RtlCliOpenInputDevice(&hKeyboard, KeyboardType);

    // Show the main menu
    RtlCliShowMenu();

    // Should never reach here
    DeinitHeapMemory(hHeap);
    NtTerminateProcess(NtCurrentProcess(), 0);

    return STATUS_SUCCESS;
}
