/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            gfx.h
 * DESCRIPTION:     Buffered display subsystem.
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#ifndef NATIVE_GFX_H
#define NATIVE_GFX_H

#define GFX_W 160
#define GFX_H 60

VOID GfxInit(VOID);
VOID GfxClear(WCHAR Ch);
VOID GfxPut(ULONG X, ULONG Y, WCHAR Ch);
VOID GfxPresent(VOID);
VOID GfxScrollClear(VOID);

#endif
