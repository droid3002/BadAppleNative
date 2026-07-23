/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            gfx.c
 * DESCRIPTION:     Buffered display subsystem. Sends entire frames
 *                  as single NtDisplayString calls for smooth output.
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#include "precomp.h"
#include "gfx.h"

static WCHAR BackBuf[GFX_H][GFX_W + 1];

VOID GfxInit(VOID)
{
    ULONG x, y;
    for (y = 0; y < GFX_H; y++)
    {
        for (x = 0; x < GFX_W; x++)
            BackBuf[y][x] = L' ';
        BackBuf[y][GFX_W] = UNICODE_NULL;
    }
}

VOID GfxClear(WCHAR Ch)
{
    ULONG x, y;
    for (y = 0; y < GFX_H; y++)
    {
        for (x = 0; x < GFX_W; x++)
            BackBuf[y][x] = Ch;
    }
}

VOID GfxPut(ULONG X, ULONG Y, WCHAR Ch)
{
    if (X < GFX_W && Y < GFX_H)
        BackBuf[Y][X] = Ch;
}

VOID GfxPresent(VOID)
{
    ULONG y, pos;
    UNICODE_STRING frame;
    WCHAR frameBuf[(GFX_W + 3) * GFX_H + 1];

    pos = 0;

    for (y = 0; y < GFX_H; y++)
    {
        ULONG x;
        for (x = 0; x < GFX_W; x++)
            frameBuf[pos++] = BackBuf[y][x];
        frameBuf[pos++] = L'\r';
        frameBuf[pos++] = L'\n';
    }
    frameBuf[pos] = UNICODE_NULL;

    frame.Buffer = frameBuf;
    frame.Length = pos * sizeof(WCHAR);
    frame.MaximumLength = sizeof(frameBuf);
    NtDisplayString(&frame);
}

#define SCROLL_W 75
#define SCROLL_ROWS 46

VOID GfxScrollClear(VOID)
{
    UNICODE_STRING blank;
    WCHAR blankBuf[(SCROLL_W + 2) * SCROLL_ROWS + 1];
    ULONG i, pos;

    pos = 0;
    for (i = 0; i < SCROLL_ROWS; i++)
    {
        ULONG x;
        for (x = 0; x < SCROLL_W; x++)
            blankBuf[pos++] = L' ';
        blankBuf[pos++] = L'\n';
    }
    blankBuf[pos] = UNICODE_NULL;

    blank.Buffer = blankBuf;
    blank.Length = pos * sizeof(WCHAR);
    blank.MaximumLength = sizeof(blankBuf);
    NtDisplayString(&blank);
}
