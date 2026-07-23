/**
 * installer.c - Win32 installer for NativeShell
 *
 * This is a normal Win32 executable that installs NativeShell
 * into the Windows boot sequence. Double-click to run.
 *
 * It copies files, sets the BootExecute registry key, then
 * prompts you to reboot. After reboot, NativeShell runs at boot
 * as a native subsystem process (before the desktop).
 *
 * Build: i686-w64-mingw32-gcc -o installer.exe installer.c -ladvapi32 -luser32 -mwindows -O2 -w
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define INSTALL_DIR     "C:\\NativeShell"
#define REG_SESSION_MGR "SYSTEM\\CurrentControlSet\\Control\\Session Manager"

/* Extract embedded resources.
 * The .dat and .sys files are appended to this .exe by the build script.
 * We find them by reading our own PE sections. */

static HMODULE g_hSelf = NULL;

/* Simple resource extraction: read appended data after PE image */
static DWORD GetSelfSize(void)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)g_hSelf;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)dos + dos->e_lfanew);
    DWORD sizeOfImage = nt->OptionalHeader.SizeOfImage;
    /* The appended data starts after the PE image (aligned to file alignment) */
    DWORD fileAlignment = nt->OptionalHeader.FileAlignment;
    /* Total file size from DOS header */
    return sizeOfImage;
}

/* Append a file from resource section or embedded data */
static BOOL ExtractFile(const char *resName, const char *outPath)
{
    HRSRC hRes = FindResourceA(g_hSelf, resName, "DATA");
    if (!hRes) return FALSE;

    HGLOBAL hData = LoadResource(g_hSelf, hRes);
    if (!hData) return FALSE;

    DWORD size = SizeofResource(g_hSelf, hRes);
    LPVOID data = LockResource(hData);
    if (!data || size == 0) return FALSE;

    HANDLE hFile = CreateFileA(outPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written;
    WriteFile(hFile, data, size, &written, NULL);
    CloseHandle(hFile);
    return (written == size);
}

/* Show a message box */
static void ShowMsg(const char *title, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    MessageBoxA(NULL, buf, title, MB_OK | MB_ICONINFORMATION);
}

/* Show error and exit */
static void ShowErr(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    MessageBoxA(NULL, buf, "NativeShell Installer - Error", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

/* Create the install directory */
static void CreateInstallDir(void)
{
    if (!CreateDirectoryA(INSTALL_DIR, NULL))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
            ShowErr("Failed to create directory %s (error %lu)", INSTALL_DIR, err);
    }
}

/* Copy a file to the install directory */
static void CopyToInstall(const char *fileName, const void *data, DWORD size)
{
    char path[MAX_PATH];
    HANDLE hFile;
    DWORD written;

    sprintf(path, "%s\\%s", INSTALL_DIR, fileName);

    hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        ShowErr("Failed to create %s", path);

    if (!WriteFile(hFile, data, size, &written, NULL) || written != size)
    {
        CloseHandle(hFile);
        ShowErr("Failed to write %s", path);
    }

    CloseHandle(hFile);
}

/* Load a file into memory */
static void *LoadFile(const char *path, DWORD *outSize)
{
    HANDLE hFile;
    DWORD size, read;
    void *data;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0)
    {
        CloseHandle(hFile);
        return NULL;
    }

    data = HeapAlloc(GetProcessHeap(), 0, size);
    if (!data)
    {
        CloseHandle(hFile);
        return NULL;
    }

    ReadFile(hFile, data, size, &read, NULL);
    CloseHandle(hFile);

    if (read != size)
    {
        HeapFree(GetProcessHeap(), 0, data);
        return NULL;
    }

    *outSize = size;
    return data;
}

/* Run a command silently (no console window) and return exit code */
static DWORD RunSilent(const char *cmd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exitCode = 1;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 30000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return exitCode;
}

/* Disable driver signature enforcement via bcdedit */
static void DisableDriverSigning(void)
{
    DWORD rc;

    /* Test signing mode - allows loading unsigned .sys files */
    rc = RunSilent("bcdedit /set testsigning on");

    /* Integrity checks off - more aggressive, but ensures it works */
    RunSilent("bcdedit /set nointegritychecks on");
}

/* Create the driver service registry key */
static void CreateDriverServiceKey(void)
{
    HKEY hKey;
    LONG result;
    char sysRoot[MAX_PATH];
    char imagePath[MAX_PATH];
    DWORD val;

    result = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                             "SYSTEM\\CurrentControlSet\\Services\\BadAppleAudio",
                             0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS)
        return;

    /* Type = 1 (KERNEL_DRIVER) */
    val = 1;
    RegSetValueExA(hKey, "Type", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

    /* Start = 0 (BOOT - loaded by kernel before smss.exe) */
    val = 0;
    RegSetValueExA(hKey, "Start", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

    /* ErrorControl = 1 (NORMAL - ignore failure, continue boot) */
    val = 1;
    RegSetValueExA(hKey, "ErrorControl", 0, REG_DWORD, (BYTE*)&val, sizeof(val));

    /* ImagePath = \SystemRoot\System32\drivers\BadAppleAudio.sys */
    GetWindowsDirectoryA(sysRoot, MAX_PATH);
    sprintf(imagePath, "\\SystemRoot\\System32\\drivers\\BadAppleAudio.sys");
    RegSetValueExA(hKey, "ImagePath", 0, REG_SZ, (BYTE*)imagePath, strlen(imagePath) + 1);

    RegCloseKey(hKey);
}

/* Restore driver signing enforcement */
static void RestoreDriverSigning(void)
{
    RunSilent("bcdedit /set testsigning off");
    RunSilent("bcdedit /set nointegritychecks off");
}

/* Set the BootExecute registry value */
static void SetBootExecute(void)
{
    HKEY hKey;
    LONG result;
    const char *bootExec = "C:\\NativeShell\\native.exe";

    result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, REG_SESSION_MGR,
                           0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS)
        ShowErr("Failed to open Session Manager registry key (error %ld)\n"
                "Make sure you're running as Administrator.", result);

    result = RegSetValueExA(hKey, "BootExecute", 0, REG_MULTI_SZ,
                            (const BYTE *)bootExec, strlen(bootExec) + 2);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS)
        ShowErr("Failed to set BootExecute registry value (error %ld)\n"
                "Make sure you're running as Administrator.", result);
}

/* Restore normal BootExecute */
static void RestoreBootExecute(void)
{
    HKEY hKey;
    LONG result;
    const char *bootExec = "autocheck autochk *";

    result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, REG_SESSION_MGR,
                           0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) return;

    RegSetValueExA(hKey, "BootExecute", 0, REG_MULTI_SZ,
                   (const BYTE *)bootExec, strlen(bootExec) + 2);
    RegCloseKey(hKey);
}

/* Uninstall: restore BootExecute and remove files */
static void DoUninstall(void)
{
    char path[MAX_PATH];
    int confirm;

    confirm = MessageBoxA(NULL,
        "This will restore normal Windows boot and remove NativeShell.\n"
        "Continue?",
        "NativeShell Installer - Uninstall",
        MB_YESNO | MB_ICONQUESTION);

    if (confirm != IDYES) return;

    RestoreBootExecute();
    RestoreDriverSigning();

    /* Delete files */
    sprintf(path, "%s\\native.exe", INSTALL_DIR);
    DeleteFileA(path);
    sprintf(path, "%s\\badapple.dat", INSTALL_DIR);
    DeleteFileA(path);
    sprintf(path, "%s\\badapple_audio.dat", INSTALL_DIR);
    DeleteFileA(path);
    sprintf(path, "%s\\BadAppleAudio.sys", INSTALL_DIR);
    DeleteFileA(path);
    sprintf(path, "%s\\installer.exe", INSTALL_DIR);
    DeleteFileA(path);
    RemoveDirectoryA(INSTALL_DIR);

    MessageBoxA(NULL,
        "NativeShell has been uninstalled.\n"
        "Windows will boot normally on next restart.",
        "NativeShell Installer",
        MB_OK | MB_ICONINFORMATION);
}

/* Main install routine */
static void DoInstall(void)
{
    DWORD dataSize;
    void *data;

    /* Check if running as admin */
    {
        BOOL isAdmin = FALSE;
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        {
            DWORD needed;
            TOKEN_ELEVATION elevation;
            if (GetTokenInformation(hToken, TokenElevation,
                                    &elevation, sizeof(elevation), &needed))
            {
                isAdmin = elevation.TokenIsElevated;
            }
            CloseHandle(hToken);
        }

        if (!isAdmin)
        {
            ShowErr("This installer must be run as Administrator.\n\n"
                    "Right-click installer.exe and select\n"
                    "'Run as administrator'.");
            return;
        }
    }

    /* Show progress via message boxes */

    ShowMsg("NativeShell Installer",
            "Welcome to NativeShell Installer!\n\n"
            "This will install:\n"
            "  - NativeShell v0.15.0 (native subsystem shell)\n"
            "  - Bad Apple!! ASCII animation\n"
            "  - AC97 audio driver (BadAppleAudio.sys)\n\n"
            "Installation directory: %s\n\n"
            "Click OK to continue.", INSTALL_DIR);

    /* Step 1: Create directory */
    CreateInstallDir();

    /* Step 2: Copy files from the same directory as installer.exe */
    {
        char srcPath[MAX_PATH];
        char dstPath[MAX_PATH];
        char *lastSlash;
        DWORD size;

        /* Get installer's own directory */
        GetModuleFileNameA(NULL, srcPath, MAX_PATH);
        lastSlash = strrchr(srcPath, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';

        /* Copy native.exe */
        sprintf(dstPath, "%s\\native.exe", INSTALL_DIR);
        sprintf(srcPath + strlen(srcPath), "native.exe");
        if (!CopyFileA(srcPath, dstPath, FALSE))
            ShowErr("Cannot find native.exe in the same directory as installer.exe");
        /* Remove filename from srcPath */
        srcPath[strlen(srcPath) - strlen("native.exe")] = '\0';

        /* Copy badapple.dat */
        sprintf(dstPath, "%s\\badapple.dat", INSTALL_DIR);
        sprintf(srcPath + strlen(srcPath), "badapple.dat");
        if (!CopyFileA(srcPath, dstPath, FALSE))
            ShowErr("Cannot find badapple.dat in the same directory as installer.exe");
        srcPath[strlen(srcPath) - strlen("badapple.dat")] = '\0';

        /* Copy badapple_audio.dat */
        sprintf(dstPath, "%s\\badapple_audio.dat", INSTALL_DIR);
        sprintf(srcPath + strlen(srcPath), "badapple_audio.dat");
        if (!CopyFileA(srcPath, dstPath, FALSE))
            ShowErr("Cannot find badapple_audio.dat in the same directory as installer.exe");
        srcPath[strlen(srcPath) - strlen("badapple_audio.dat")] = '\0';

        /* Copy BadAppleAudio.sys */
        sprintf(dstPath, "%s\\BadAppleAudio.sys", INSTALL_DIR);
        sprintf(srcPath + strlen(srcPath), "BadAppleAudio.sys");
        if (!CopyFileA(srcPath, dstPath, FALSE))
            ShowErr("Cannot find BadAppleAudio.sys in the same directory as installer.exe");

        /* Also copy driver to System32\drivers so boot loader can find it */
        {
            char driversPath[MAX_PATH];
            GetWindowsDirectoryA(driversPath, MAX_PATH);
            strcat(driversPath, "\\System32\\drivers\\BadAppleAudio.sys");
            if (!CopyFileA(srcPath, driversPath, FALSE))
                ShowErr("Failed to copy driver to System32\\drivers");
        }
    }

    /* Step 3: Set BootExecute registry */
    SetBootExecute();

    /* Step 4: Create driver service key (Start=0 BOOT) */
    CreateDriverServiceKey();

    /* Step 5: Disable driver signature enforcement so BadAppleAudio.sys can load */
    DisableDriverSigning();

    /* Step 4: Done - prompt for reboot */
    {
        int result;

        result = MessageBoxA(NULL,
            "Installation complete!\n\n"
            "Files installed to: C:\\NativeShell\\\n"
            "BootExecute registry set to run NativeShell at boot.\n"
            "Driver signature enforcement DISABLED.\n\n"
            "The computer MUST be rebooted for NativeShell to start.\n\n"
            "Reboot now?",
            "NativeShell Installer - Success",
            MB_YESNO | MB_ICONINFORMATION);

        if (result == IDYES)
        {
            /* Initiate reboot */
            HANDLE hToken;
            TOKEN_PRIVILEGES tp;

            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            {
                LookupPrivilegeValueA(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
                CloseHandle(hToken);
            }

            ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
        }
    }
}

/* Entry point */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    g_hSelf = hInstance;

    /* Check command line for uninstall */
    if (strstr(GetCommandLineA(), "--uninstall") || strstr(GetCommandLineA(), "/uninstall"))
    {
        /* Check admin */
        BOOL isAdmin = FALSE;
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        {
            DWORD needed;
            TOKEN_ELEVATION elevation;
            if (GetTokenInformation(hToken, TokenElevation,
                                    &elevation, sizeof(elevation), &needed))
            {
                isAdmin = elevation.TokenIsElevated;
            }
            CloseHandle(hToken);
        }
        if (!isAdmin)
        {
            ShowErr("Uninstaller must be run as Administrator.");
            return 1;
        }
        DoUninstall();
        return 0;
    }

    /* Normal install */
    DoInstall();
    return 0;
}
