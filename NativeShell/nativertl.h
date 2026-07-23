/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            nativertl.h
 * DESCRIPTION:     Minimal CRT replacements for native subsystem.
 *                  All functions use only ntdll - no kernel32/msvcrt.
 */

#ifndef NATIVE_RTL_H
#define NATIVE_RTL_H

#include <stdarg.h>

/* Heap */
void *malloc(unsigned int size);
void *calloc(unsigned int count, unsigned int size);
void free(void *ptr);

/* String */
unsigned int strlen(const char *s);
unsigned int strnlen(const char *s, unsigned int maxlen);
char *strncpy(char *dst, const char *src, unsigned int n);
int strcmp(const char *s1, const char *s2);
int _strnicmp(const char *s1, const char *s2, unsigned int n);
char *strchr(const char *s, int c);
unsigned int strspn(const char *s, const char *accept);
unsigned int strcspn(const char *s, const char *reject);

unsigned int wcslen(const wchar_t *s);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, unsigned int n);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcscat(wchar_t *dst, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, unsigned int n);

/* Formatted output */
int sprintf(char *buf, const char *fmt, ...);
int _vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap);
int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap);
int swprintf(wchar_t *buf, unsigned int size, const char *fmt, ...);

/* Memory */
void *memcpy(void *dst, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);
int memcmp(const void *s1, const void *s2, unsigned int n);

/* Conversion */
int atoi(const char *s);

/* Entry point */
void NtProcessStartup(void *Peb);

#endif
