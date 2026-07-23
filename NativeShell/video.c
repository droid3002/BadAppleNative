/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            video.c
 * DESCRIPTION:     Reads the BAPL video format. Supports reading from a file
 *                  or from the embedded badapple.dat baked into the executable.
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#include "precomp.h"
#include "video.h"

/*++
 * @name VideoOpen
 *
 * Opens a BAPL video file and reads the header.
 * First tries the filesystem path, then falls back to embedded data.
 *
 *--*/
BOOLEAN VideoOpen(PVIDEO_CONTEXT ctx, PCWSTR Filename)
{
    UCHAR header[VIDEO_HEADER_SIZE];
    DWORD bytesRead = 0;

    memset(ctx, 0, sizeof(VIDEO_CONTEXT));

    /* Try opening from filesystem first */
    if (NtFileOpenFile(&ctx->hFile, (PWCHAR)Filename, FALSE, FALSE))
    {
        if (NtFileReadFile(ctx->hFile, header, VIDEO_HEADER_SIZE, &bytesRead) &&
            bytesRead == VIDEO_HEADER_SIZE)
        {
            goto parse_header;
        }
        NtFileCloseFile(ctx->hFile);
        ctx->hFile = NULL;
    }

    /* Fall back to embedded data */
    if ((ULONG_PTR)binary_badapple_dat_size > 0)
    {
        ctx->memBase = (PUCHAR)binary_badapple_dat_start;
        ctx->memCursor = ctx->memBase + VIDEO_HEADER_SIZE;
        ctx->memEnd = ctx->memBase + (ULONG_PTR)binary_badapple_dat_size;

        RtlCopyMemory(header, ctx->memBase, VIDEO_HEADER_SIZE);
        goto parse_header;
    }

    RtlCliDisplayString("No video data available\n");
    return FALSE;

parse_header:
    if (header[0] != BAPL_MAGIC_0 || header[1] != BAPL_MAGIC_1 ||
        header[2] != BAPL_MAGIC_2 || header[3] != BAPL_MAGIC_3)
    {
        RtlCliDisplayString("Invalid video file (bad magic)\n");
        VideoClose(ctx);
        return FALSE;
    }

    ctx->width = *(USHORT *)(header + 4);
    ctx->height = *(USHORT *)(header + 6);
    ctx->fps = *(USHORT *)(header + 8);
    ctx->frameCount = *(UINT *)(header + 10);
    ctx->currentFrame = 0;

    RtlCliDisplayString("Video: %ux%u @ %ufps, %u frames\n",
                        ctx->width, ctx->height, ctx->fps, ctx->frameCount);

    return TRUE;
}

/*++
 * @name VideoNextFrame
 *
 * Reads the next packed 1-bit frame from the video.
 *
 *--*/
BOOLEAN VideoNextFrame(PVIDEO_CONTEXT ctx, PUCHAR FrameBuf, ULONG BufSize)
{
    ULONG frameSize;

    if (ctx->currentFrame >= ctx->frameCount)
        return FALSE;

    frameSize = VIDEO_FRAME_SIZE(ctx->width, ctx->height);
    if (frameSize > BufSize)
        return FALSE;

    /* Read from file */
    if (ctx->hFile)
    {
        DWORD bytesRead = 0;
        if (!NtFileReadFile(ctx->hFile, FrameBuf, frameSize, &bytesRead) ||
            bytesRead != frameSize)
            return FALSE;
    }
    /* Read from embedded memory */
    else if (ctx->memBase)
    {
        if (ctx->memCursor + frameSize > ctx->memEnd)
            return FALSE;
        RtlCopyMemory(FrameBuf, ctx->memCursor, frameSize);
        ctx->memCursor += frameSize;
    }
    else
    {
        return FALSE;
    }

    ctx->currentFrame++;
    return TRUE;
}

/*++
 * @name VideoClose
 *
 * Closes the video file handle and resets state.
 *
 *--*/
VOID VideoClose(PVIDEO_CONTEXT ctx)
{
    if (ctx->hFile)
    {
        NtFileCloseFile(ctx->hFile);
        ctx->hFile = NULL;
    }
    ctx->memBase = NULL;
    ctx->memCursor = NULL;
    ctx->memEnd = NULL;
}
