/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            nativertl.c
 * DESCRIPTION:     Minimal CRT replacements for native subsystem.
 *                  Only uses ntdll functions. No kernel32/msvcrt.
 */

#include "precomp.h"

/* Process heap handle, set by NtProcessStartup */
static HANDLE hNativeHeap = NULL;

/* ================================================================
 *  Heap
 * ================================================================ */

void *malloc(unsigned int size)
{
    if (!hNativeHeap) return NULL;
    return RtlAllocateHeap(hNativeHeap, 0, size);
}

void *calloc(unsigned int count, unsigned int size)
{
    unsigned int total = count * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *ptr)
{
    if (ptr && hNativeHeap)
        RtlFreeHeap(hNativeHeap, 0, ptr);
}

/* ================================================================
 *  String
 * ================================================================ */

unsigned int strlen(const char *s)
{
    unsigned int i = 0;
    if (!s) return 0;
    while (s[i]) i++;
    return i;
}

unsigned int strnlen(const char *s, unsigned int maxlen)
{
    unsigned int i = 0;
    if (!s) return 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

char *strncpy(char *dst, const char *src, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int _strnicmp(const char *s1, const char *s2, unsigned int n)
{
    unsigned int i;
    char a, b;
    for (i = 0; i < n; i++) {
        a = s1[i]; b = s2[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return (unsigned char)a - (unsigned char)b;
        if (!a) break;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : (void *)0;
}

unsigned int strspn(const char *s, const char *accept)
{
    unsigned int count = 0;
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*s == *a) goto next;
            a++;
        }
        break;
    next:
        count++;
        s++;
    }
    return count;
}

unsigned int strcspn(const char *s, const char *reject)
{
    unsigned int count = 0;
    while (*s) {
        const char *r = reject;
        while (*r) {
            if (*s == *r) return count;
            r++;
        }
        count++;
        s++;
    }
    return count;
}

unsigned int wcslen(const wchar_t *s)
{
    unsigned int i = 0;
    if (!s) return 0;
    while (s[i]) i++;
    return i;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++));
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, unsigned int n)
{
    wchar_t *d = dst;
    while (*d) d++;
    while (n-- && (*d = *src)) { d++; src++; }
    *d = 0;
    return dst;
}

/* ================================================================
 *  Formatted output - _vsnprintf / vsnprintf / sprintf / swprintf
 *  Supports: %s %S %d %u %x %c %%
 * ================================================================ */

static void fmt_write_char(char *buf, unsigned int size, unsigned int *pos, char c)
{
    if (*pos < size - 1) buf[*pos] = c;
    (*pos)++;
}

static void fmt_write_str(char *buf, unsigned int size, unsigned int *pos, const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++)
        fmt_write_char(buf, size, pos, s[i]);
}

static void fmt_write_wstr(char *buf, unsigned int size, unsigned int *pos, const wchar_t *ws, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        wchar_t wc = ws[i];
        fmt_write_char(buf, size, pos, (wc < 128) ? (char)wc : '?');
    }
}

static void fmt_utoa_dec(char *buf, unsigned int size, unsigned int *pos, unsigned int val)
{
    char tmp[12];
    int len = 0;
    unsigned int v = val;
    int i;
    if (v == 0) { tmp[len++] = '0'; }
    else { while (v) { tmp[len++] = '0' + (v % 10); v /= 10; } }
    for (i = len - 1; i >= 0; i--)
        fmt_write_char(buf, size, pos, tmp[i]);
}

static void fmt_itoa_dec(char *buf, unsigned int size, unsigned int *pos, int val)
{
    if (val < 0) {
        fmt_write_char(buf, size, pos, '-');
        fmt_utoa_dec(buf, size, pos, (unsigned int)(-(val + 1)) + 1u);
    } else {
        fmt_utoa_dec(buf, size, pos, (unsigned int)val);
    }
}

static void fmt_utoa_hex(char *buf, unsigned int size, unsigned int *pos, unsigned int val)
{
    char tmp[9];
    int len = 0;
    unsigned int v = val;
    int i;
    const char *hex = "0123456789abcdef";
    if (v == 0) { tmp[len++] = '0'; }
    else { while (v) { tmp[len++] = hex[v & 0xf]; v >>= 4; } }
    for (i = len - 1; i >= 0; i--)
        fmt_write_char(buf, size, pos, tmp[i]);
}

static int native_vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
{
    unsigned int pos = 0;
    if (size == 0) return 0;
    size--;

    while (*fmt && pos < size) {
        if (*fmt != '%') {
            fmt_write_char(buf, size, &pos, *fmt);
            fmt++;
            continue;
        }
        fmt++;

        if (*fmt == 'S') {
            const wchar_t *ws = va_arg(ap, const wchar_t *);
            if (ws) {
                unsigned int wlen = 0;
                const wchar_t *p = ws;
                while (*p++) wlen++;
                fmt_write_wstr(buf, size, &pos, ws, wlen);
            }
            fmt++;
            continue;
        }

        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            fmt_write_str(buf, size, &pos, s, strlen(s));
            fmt++;
            continue;
        }

        if (*fmt == 'd' || *fmt == 'i') {
            fmt_itoa_dec(buf, size, &pos, va_arg(ap, int));
            fmt++;
            continue;
        }

        if (*fmt == 'u') {
            fmt_utoa_dec(buf, size, &pos, va_arg(ap, unsigned int));
            fmt++;
            continue;
        }

        if (*fmt == 'x' || *fmt == 'X') {
            fmt_utoa_hex(buf, size, &pos, va_arg(ap, unsigned int));
            fmt++;
            continue;
        }

        if (*fmt == 'c') {
            fmt_write_char(buf, size, &pos, (char)va_arg(ap, int));
            fmt++;
            continue;
        }

        if (*fmt == '%') {
            fmt_write_char(buf, size, &pos, '%');
            fmt++;
            continue;
        }

        fmt_write_char(buf, size, &pos, '%');
        fmt_write_char(buf, size, &pos, *fmt);
        fmt++;
    }

    buf[pos] = 0;
    return (int)pos;
}

/* Public name used by display.c */
int _vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
{
    return native_vsnprintf(buf, size, fmt, ap);
}

int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
{
    return native_vsnprintf(buf, size, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = native_vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return ret;
}

int swprintf(wchar_t *buf, unsigned int size, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    unsigned int i;
    int ret;

    va_start(ap, fmt);
    ret = native_vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    for (i = 0; i < size && tmp[i]; i++)
        buf[i] = (wchar_t)tmp[i];
    if (i < size) buf[i] = 0;

    return ret;
}

/* ================================================================
 *  Memory
 * ================================================================ */

void *memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, unsigned int n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    while (n--) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return 0;
}

/* ================================================================
 *  Conversion
 * ================================================================ */

int atoi(const char *s)
{
    int result = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9')
        result = result * 10 + (*s++ - '0');
    return result * sign;
}

/* ================================================================
 *  DbgPrint - redirect to NtDisplayString
 * ================================================================ */

ULONG __cdecl DbgPrint(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    UNICODE_STRING ustr;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    RtlInitUnicodeString(&ustr, buf);
    NtDisplayString(&ustr);
    return 0UL;
}

/* ================================================================
 *  NtProcessStartup - Native Mode Entry Point
 *  Called by the NT native subsystem loader.
 *  The linker --entry,_NtProcessStartup resolves to this
 *  (MinGW cdecl convention adds _ prefix to NtProcessStartup).
 * ================================================================ */

/* Forward declaration */
extern NTSTATUS __cdecl shell_main(INT argc, PCHAR argv[], PCHAR envp[], ULONG DebugFlag);

static void ParseCommandLine(PUNICODE_STRING cmdLine, PCHAR *args, UINT *argCount)
{
    PCHAR start;
    UINT argc = 0;
    BOOLEAN inQuote = FALSE;
    BOOLEAN inArg = FALSE;
    WCHAR *ws = cmdLine->Buffer;
    ULONG wlen = cmdLine->Length / sizeof(WCHAR);
    CHAR tmp[512];
    ULONG i, len;

    len = wlen < sizeof(tmp) - 1 ? wlen : sizeof(tmp) - 1;
    for (i = 0; i < len; i++)
        tmp[i] = (ws[i] < 128) ? (CHAR)ws[i] : '?';
    tmp[len] = 0;

    start = tmp;

    while (*start && argc < 256) {
        if (*start == '"') {
            inQuote = !inQuote;
            if (!inArg) {
                args[argc] = start + 1;
                argc++;
                inArg = TRUE;
            }
            start++;
            continue;
        }

        if (*start == ' ' || *start == '\t') {
            if (!inQuote) {
                *start = 0;
                inArg = FALSE;
                start++;
                continue;
            }
        }

        if (!inArg) {
            args[argc] = start;
            argc++;
            inArg = TRUE;
        }

        start++;
    }

    *argCount = argc;
}

void NtProcessStartup(void *PebPtr)
{
    PPEB Peb = (PPEB)PebPtr;
    PCHAR args[256];
    UINT argc = 0;

    hNativeHeap = Peb->ProcessHeap;

    if (Peb->ProcessParameters && Peb->ProcessParameters->CommandLine.Length > 0)
        ParseCommandLine(&Peb->ProcessParameters->CommandLine, args, &argc);

    shell_main(argc, args, NULL, 0);

    NtTerminateProcess(NtCurrentProcess(), 0);
}
