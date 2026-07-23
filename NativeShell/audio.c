/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            audio.c
 * DESCRIPTION:     Audio playback via BadAppleAudio kernel driver.
 *                  Self-installs the driver if not already loaded:
 *                  1. Writes embedded .sys to disk
 *                  2. Creates registry service key
 *                  3. Calls NtLoadDriver
 *                  4. Opens the device
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#include "precomp.h"
#include "audio.h"
#include "ntreg.h"

static HANDLE hAudioDevice = NULL;
static BOOLEAN bAudioAvailable = FALSE;

/* Registry path for NtLoadDriver (NT format) */
#define DRIVER_SERVICE_PATH L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\BadAppleAudio"
#define DRIVER_REG_SUBKEY    L"SYSTEM\\CurrentControlSet\\Services\\BadAppleAudio"

/*++
 * @name AudioWriteDriverToFile
 *
 * Writes the embedded BadAppleAudio.sys to the current directory.
 * Returns the full NT path (with \??\ prefix) for NtLoadDriver.
 *
 *--*/
static BOOLEAN AudioWriteDriverToFile(PWSTR NtPathOut, ULONG NtPathOutSize)
{
    HANDLE hFile;
    DWORD written;
    WCHAR filePath[MAX_PATH];
    WCHAR ntPath[MAX_PATH];
    WCHAR winDir[MAX_PATH];
    ULONG driverSize;

    driverSize = (ULONG)((ULONG_PTR)binary_BadAppleAudio_sys_size);
    if (driverSize == 0)
    {
        RtlCliDisplayString("[Audio] No embedded driver data\n");
        return FALSE;
    }

    /* Write to System32\drivers — the only place boot-load drivers can be found */
    RtlCliGetCurrentDirectory(winDir);
    /* Build C:\Windows\System32\drivers\BadAppleAudio.sys */
    wcscpy(filePath, L"\\??\\");
    wcscat(filePath, winDir);
    /* Find the drive root, e.g. C: */
    {
        WCHAR *p = filePath;
        while (*p && *p != L'\\') p++;
        if (*p == L'\\')
        {
            /* We have "\??\C:" now, append \Windows\System32\drivers */
            wcscpy(p, L"\\Windows\\System32\\drivers\\BadAppleAudio.sys");
        }
        else
        {
            /* Fallback: use current dir */
            wcscpy(filePath, winDir);
            if (filePath[wcslen(filePath) - 1] != L'\\')
                wcscat(filePath, L"\\");
            wcscat(filePath, L"BadAppleAudio.sys");
        }
    }

    /* Build the NT path (already has \??\ prefix) */
    wcscpy(ntPath, filePath);

    /* The DOS path for NtFileOpenFile needs \??\ stripped */
    {
        WCHAR dosPath[MAX_PATH];
        wcscpy(dosPath, filePath + 4); /* skip \??\ */

        /* Check if file already exists with correct size */
        {
            LONGLONG existingSize = 0;
            if (NtFileOpenFile(&hFile, dosPath, FALSE, FALSE))
            {
                NtFileGetFileSize(hFile, &existingSize);
                NtFileCloseFile(hFile);
                if ((ULONG)existingSize == driverSize)
                {
                    wcscpy(NtPathOut, ntPath);
                    return TRUE;
                }
            }
        }

        RtlCliDisplayString("[Audio] Writing driver to %S...\n", dosPath);

        /* Create and write the .sys file */
        if (!NtFileOpenFile(&hFile, dosPath, TRUE, TRUE))
        {
            RtlCliDisplayString("[Audio] Failed to create driver file\n");
            return FALSE;
        }

        if (!NtFileWriteFile(hFile, (PVOID)binary_BadAppleAudio_sys_start, driverSize, &written))
        {
            RtlCliDisplayString("[Audio] Failed to write driver file\n");
            NtFileCloseFile(hFile);
            return FALSE;
        }

        NtFileCloseFile(hFile);

        if (written != driverSize)
        {
            RtlCliDisplayString("[Audio] Short write: %u / %u bytes\n", written, driverSize);
            return FALSE;
        }
    }

    wcscpy(NtPathOut, ntPath);
    return TRUE;
}

/*++
 * @name AudioCreateServiceKey
 *
 * Creates the registry service key for the driver.
 * NtLoadDriver requires this key to exist with Type, Start, ErrorControl, ImagePath.
 *
 *--*/
static BOOLEAN AudioCreateServiceKey(PWSTR DriverNtPath)
{
    HANDLE hKey = NULL;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING keyName;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG disposition;

    RtlInitUnicodeString(&keyName, DRIVER_SERVICE_PATH);
    InitializeObjectAttributes(&objAttr, &keyName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    /* Create or open the service key */
    status = NtCreateKey(&hKey,
                         KEY_ALL_ACCESS,
                         &objAttr,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         &disposition);

    if (!NT_SUCCESS(status))
    {
        RtlCliDisplayString("[Audio] NtCreateKey failed: 0x%X\n", status);
        return FALSE;
    }

    /* Type = 1 (KERNEL_DRIVER) */
    {
        ULONG driverType = 1;
        RtlInitUnicodeString(&valueName, L"Type");
        NtSetValueKey(hKey, &valueName, 0, REG_DWORD, &driverType, sizeof(driverType));
    }

    /* Start = 0 (BOOT - kernel loads this before smss.exe runs) */
    {
        ULONG startType = 0;
        RtlInitUnicodeString(&valueName, L"Start");
        NtSetValueKey(hKey, &valueName, 0, REG_DWORD, &startType, sizeof(startType));
    }

    /* ErrorControl = 1 (SERVICE_ERROR_NORMAL) */
    {
        ULONG errorControl = 1;
        RtlInitUnicodeString(&valueName, L"ErrorControl");
        NtSetValueKey(hKey, &valueName, 0, REG_DWORD, &errorControl, sizeof(errorControl));
    }

    /* ImagePath = \SystemRoot\System32\drivers\BadAppleAudio.sys */
    {
        UNICODE_STRING imagePath;
        RtlInitUnicodeString(&imagePath, L"\\SystemRoot\\System32\\drivers\\BadAppleAudio.sys");
        RtlInitUnicodeString(&valueName, L"ImagePath");
        NtSetValueKey(hKey, &valueName, 0, REG_EXPAND_SZ,
                      imagePath.Buffer, imagePath.Length + sizeof(WCHAR));
    }

    NtClose(hKey);
    return TRUE;
}

/*++
 * @name AudioInit
 *
 * Initializes audio by loading the BadAppleAudio kernel driver.
 * Self-installs if not already present.
 *
 * @return STATUS_SUCCESS or error code.
 *
 *--*/
NTSTATUS AudioInit(VOID)
{
    UNICODE_STRING deviceName;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;

    hAudioDevice = NULL;
    bAudioAvailable = FALSE;

    /* First, try to open the device (driver might already be loaded) */
    RtlInitUnicodeString(&deviceName, AUDIO_DEVICE_PATH);
    InitializeObjectAttributes(&objectAttributes,
                               &deviceName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    status = NtCreateFile(&hAudioDevice,
                          GENERIC_READ | GENERIC_WRITE,
                          &objectAttributes,
                          &iosb,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_OPEN,
                          0,
                          NULL,
                          0);

    if (NT_SUCCESS(status))
    {
        bAudioAvailable = TRUE;
        return STATUS_SUCCESS;
    }

    /* Driver not loaded — try to self-install */
    RtlCliDisplayString("[Audio] Driver not loaded, installing...\n");

    /* Step 1: Write embedded .sys to disk */
    {
        WCHAR driverNtPath[MAX_PATH];

        if (!AudioWriteDriverToFile(driverNtPath, MAX_PATH))
        {
            RtlCliDisplayString("[Audio] Failed to write driver file\n");
            return STATUS_UNSUCCESSFUL;
        }

        /* Step 2: Create registry service key (Start=0 BOOT so kernel loads it) */
        if (!AudioCreateServiceKey(driverNtPath))
        {
            RtlCliDisplayString("[Audio] Failed to create service key\n");
            return STATUS_UNSUCCESSFUL;
        }

        RtlCliDisplayString("[Audio] Driver installed. Reboot required to load.\n");
    }
}

/*++
 * @name AudioIsAvailable
 *--*/
BOOLEAN AudioIsAvailable(VOID)
{
    return bAudioAvailable;
}

/*++
 * @name AudioConfigure
 *--*/
NTSTATUS AudioConfigure(ULONG SampleRate, ULONG Channels, ULONG BitsPerSample)
{
    BAPA_CONFIGURE config;
    IO_STATUS_BLOCK iosb;

    if (!bAudioAvailable)
        return STATUS_UNSUCCESSFUL;

    config.SampleRate = SampleRate;
    config.Channels = Channels;
    config.BitsPerSample = BitsPerSample;

    return NtDeviceIoControlFile(hAudioDevice,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &iosb,
                                 BAPA_IOCTL_CONFIGURE,
                                 &config,
                                 sizeof(config),
                                 NULL,
                                 0);
}

/*++
 * @name AudioWrite
 *--*/
NTSTATUS AudioWrite(PVOID Data, ULONG Size)
{
    IO_STATUS_BLOCK iosb;

    if (!bAudioAvailable)
        return STATUS_UNSUCCESSFUL;

    return NtDeviceIoControlFile(hAudioDevice,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &iosb,
                                 BAPA_IOCTL_WRITE,
                                 Data,
                                 Size,
                                 NULL,
                                 0);
}

/*++
 * @name AudioStop
 *--*/
NTSTATUS AudioStop(VOID)
{
    IO_STATUS_BLOCK iosb;

    if (!bAudioAvailable)
        return STATUS_UNSUCCESSFUL;

    return NtDeviceIoControlFile(hAudioDevice,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &iosb,
                                 BAPA_IOCTL_STOP,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
}

/*++
 * @name AudioDeinit
 *--*/
VOID AudioDeinit(VOID)
{
    if (hAudioDevice)
    {
        AudioStop();
        NtClose(hAudioDevice);
        hAudioDevice = NULL;
    }
    bAudioAvailable = FALSE;
}
