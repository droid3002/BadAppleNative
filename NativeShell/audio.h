/**
 * PROJECT:         Native Shell - Bad Apple
 * COPYRIGHT:       LGPL; See LICENSE in the top level directory
 * FILE:            audio.h
 * DESCRIPTION:     Audio playback via BadAppleAudio kernel driver (AC97).
 * DEVELOPERS:      See CONTRIBUTORS.md in the top level directory
 */

#ifndef NATIVE_AUDIO_H
#define NATIVE_AUDIO_H

#define AUDIO_SAMPLE_RATE    22050
#define AUDIO_CHANNELS       1
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_BYTES_PER_SAMPLE (AUDIO_BITS_PER_SAMPLE / 8)
#define AUDIO_BYTES_PER_SEC  (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE)

/* Samples per video frame at 30fps */
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE / 30)

/* Bytes per video frame */
#define AUDIO_FRAME_BYTES (AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE)

/* Device path for the audio driver */
#define AUDIO_DEVICE_PATH L"\\Device\\BadAppleAudio"

/* IOCTL codes shared with the driver */
#define FILE_DEVICE_BADAPPLE 0x00008000

#define BAPA_IOCTL_CONFIGURE CTL_CODE(FILE_DEVICE_BADAPPLE, 0x800, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BAPA_IOCTL_WRITE     CTL_CODE(FILE_DEVICE_BADAPPLE, 0x801, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BAPA_IOCTL_STOP      CTL_CODE(FILE_DEVICE_BADAPPLE, 0x802, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _BAPA_CONFIGURE {
    ULONG SampleRate;
    ULONG Channels;
    ULONG BitsPerSample;
} BAPA_CONFIGURE, *PBAPA_CONFIGURE;

/* Embedded data symbols (provided by objcopy) */
extern const char binary_badapple_audio_dat_start[];
extern const char binary_badapple_audio_dat_end[];
extern const char binary_badapple_audio_dat_size[];

extern const char binary_BadAppleAudio_sys_start[];
extern const char binary_BadAppleAudio_sys_end[];
extern const char binary_BadAppleAudio_sys_size[];

/* Audio API */
NTSTATUS AudioInit(VOID);
NTSTATUS AudioConfigure(ULONG SampleRate, ULONG Channels, ULONG BitsPerSample);
NTSTATUS AudioWrite(PVOID Data, ULONG Size);
NTSTATUS AudioStop(VOID);
VOID AudioDeinit(VOID);
BOOLEAN AudioIsAvailable(VOID);

#endif
