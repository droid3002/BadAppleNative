/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            video.h
 * DESCRIPTION:     BAPL video format decoder. Supports file and embedded modes.
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#ifndef NATIVE_VIDEO_H
#define NATIVE_VIDEO_H

#define BAPL_MAGIC_0 'B'
#define BAPL_MAGIC_1 'A'
#define BAPL_MAGIC_2 'P'
#define BAPL_MAGIC_3 'L'

#define VIDEO_FRAME_SIZE(w, h) ((ULONG)((w) * (h) / 8))
#define VIDEO_HEADER_SIZE 14

typedef struct _VIDEO_CONTEXT
{
    HANDLE hFile;
    USHORT width;
    USHORT height;
    USHORT fps;
    UINT frameCount;
    UINT currentFrame;
    PUCHAR memBase;
    PUCHAR memCursor;
    PUCHAR memEnd;
} VIDEO_CONTEXT, *PVIDEO_CONTEXT;

extern const char binary_badapple_dat_start[];
extern const char binary_badapple_dat_end[];
extern const char binary_badapple_dat_size[];

BOOLEAN VideoOpen(PVIDEO_CONTEXT ctx, PCWSTR Filename);
BOOLEAN VideoNextFrame(PVIDEO_CONTEXT ctx, PUCHAR FrameBuf, ULONG BufSize);
VOID VideoClose(PVIDEO_CONTEXT ctx);

#endif
