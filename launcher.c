/**
 * launcher.c - Win32 launcher for NativeShell
 *
 * This is a normal Win32 executable that spawns the native subsystem
 * shell using NTDLL's RtlCreateUserProcess. Just double-click this
 * on the Windows desktop.
 *
 * Build: i686-w64-mingw32-gcc -o launcher.exe launcher.c -lntdll -mconsole
 */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>

/* NTDLL RtlCreateUserProcess - creates a process in any subsystem */
typedef NTSTATUS (NTAPI *fnRtlCreateUserProcess)(
    PUNICODE_STRING ImagePathName,
    ULONG CreateOptions,
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    PSECURITY_DESCRIPTOR ProcessSecurityDescriptor,
    PSECURITY_DESCRIPTOR ThreadSecurityDescriptor,
    HANDLE ParentProcess,
    BOOLEAN InheritHandles,
    HANDLE DebugPort,
    HANDLE ExceptionPort,
    PHANDLE ProcessHandle);

/* RtlInitUnicodeString helper */
static void RtlInitUnicodeStringLocal(PUNICODE_STRING dst, PCWSTR src)
{
    USHORT len = 0;
    if (src) {
        while (src[len]) len++;
        len = (len + 1) * sizeof(WCHAR);
    }
    dst->Length = len ? len - sizeof(WCHAR) : 0;
    dst->MaximumLength = len;
    dst->Buffer = (PWSTR)src;
}

/* RtlCreateProcessParameters for building the process parameters block */
typedef NTSTATUS (NTAPI *fnRtlCreateProcessParameters)(
    PRTL_USER_PROCESS_PARAMETERS *ProcessParameters,
    PUNICODE_STRING ImagePathName,
    PUNICODE_STRING DllPath,
    PUNICODE_STRING CurrentDirectory,
    PUNICODE_STRING CommandLine,
    PSECURITY_DESCRIPTOR ProcessSecurityDescriptor,
    PUNICODE_STRING WindowTitle,
    PUNICODE_STRING DesktopInfo,
    PUNICODE_STRING ShellInfo,
    PUNICODE_STRING RuntimeData);

int main(void)
{
    HMODULE ntdll;
    fnRtlCreateUserProcess pRtlCreateUserProcess;
    fnRtlCreateProcessParameters pRtlCreateProcessParameters;
    WCHAR exePath[MAX_PATH];
    WCHAR dirPath[MAX_PATH];
    WCHAR cmdLine[MAX_PATH];
    UNICODE_STRING uniExePath;
    UNICODE_STRING uniCommandLine;
    UNICODE_STRING uniCurrentDir;
    UNICODE_STRING uniDllPath;
    PRTL_USER_PROCESS_PARAMETERS procParams = NULL;
    HANDLE hProcess = NULL;
    NTSTATUS status;
    DWORD len;

    /* Get our own directory */
    len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        printf("Failed to get module path\n");
        return 1;
    }

    /* Find the last backslash to get directory */
    {
        WCHAR *p = exePath + len;
        while (p > exePath && *(p - 1) != L'\\') p--;
        *p = L'\0';
    }
    wcscpy(dirPath, exePath);

    /* Build path to native.exe in same directory */
    wcscat(exePath, L"native.exe");

    /* Check if native.exe exists */
    {
        HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ,
                                    NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            printf("native.exe not found in %S\n", dirPath);
            printf("Make sure launcher.exe is in the same folder as native.exe\n");
            return 1;
        }
        CloseHandle(hFile);
    }

    /* Load NTDLL functions */
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        printf("Failed to get ntdll\n");
        return 1;
    }

    pRtlCreateUserProcess = (fnRtlCreateUserProcess)
        GetProcAddress(ntdll, "RtlCreateUserProcess");
    pRtlCreateProcessParameters = (fnRtlCreateProcessParameters)
        GetProcAddress(ntdll, "RtlCreateProcessParameters");

    if (!pRtlCreateUserProcess || !pRtlCreateProcessParameters) {
        printf("Failed to get NTDLL functions\n");
        return 1;
    }

    /* Build NT path: \??\C:\path\to\native.exe */
    {
        WCHAR ntPath[MAX_PATH] = L"\\??\\";
        wcscat(ntPath, exePath);
        RtlInitUnicodeStringLocal(&uniExePath, ntPath);
    }

    RtlInitUnicodeStringLocal(&uniCommandLine, exePath);
    RtlInitUnicodeStringLocal(&uniCurrentDir, dirPath);
    RtlInitUnicodeStringLocal(&uniDllPath, L"");

    /* Create process parameters */
    status = pRtlCreateProcessParameters(&procParams,
                                         &uniExePath,
                                         &uniDllPath,
                                         &uniCurrentDir,
                                         &uniCommandLine,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);
    if (status < 0) {
        printf("RtlCreateProcessParameters failed: 0x%08X\n", (unsigned)status);
        return 1;
    }

    printf("Launching NativeShell (native subsystem)...\n");
    printf("Path: %S\n", exePath);

    /* Create the native process */
    status = pRtlCreateUserProcess(&uniExePath,
                                    0, /* CreateOptions */
                                    procParams,
                                    NULL, /* ProcessSecurity */
                                    NULL, /* ThreadSecurity */
                                    NULL, /* ParentProcess (current) */
                                    FALSE, /* InheritHandles */
                                    NULL, /* DebugPort */
                                    NULL, /* ExceptionPort */
                                    &hProcess);

    if (status < 0) {
        printf("RtlCreateUserProcess failed: 0x%08X\n", (unsigned)status);
        printf("Try running as Administrator, or use the boot method.\n");
        return 1;
    }

    printf("NativeShell started! (PID handle: %p)\n", hProcess);

    /* Wait for it to finish */
    WaitForSingleObject(hProcess, INFINITE);
    CloseHandle(hProcess);

    printf("NativeShell exited.\n");
    return 0;
}
