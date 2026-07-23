/**
 * PROJECT:         BadAppleAudio Driver
 * COPYRIGHT:       See LICENSE in the top level directory
 * FILE:            ac97drv.h
 * DESCRIPTION:     AC97 audio driver for NativeShell Bad Apple player.
 *                  Provides PCM playback through the AC97 codec.
 * DEVELOPERS:      Droid3002
 */

#ifndef AC97DRV_H
#define AC97DRV_H

#include <ntddk.h>
#include <wdm.h>

/* Device name shared between driver and user-mode */
#define DEVICE_NAME_STRING     L"\\Device\\BadAppleAudio"
#define SYMBOLIC_NAME_STRING   L"\\??\\BadAppleAudioLink"

/* IOCTL codes (must match audio.h in NativeShell) */
#define FILE_DEVICE_BADAPPLE 0x00008000

#define BAPA_IOCTL_CONFIGURE CTL_CODE(FILE_DEVICE_BADAPPLE, 0x800, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BAPA_IOCTL_WRITE     CTL_CODE(FILE_DEVICE_BADAPPLE, 0x801, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)
#define BAPA_IOCTL_STOP      CTL_CODE(FILE_DEVICE_BADAPPLE, 0x802, \
    METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Configuration structure from user mode */
typedef struct _BAPA_CONFIGURE {
    ULONG SampleRate;
    ULONG Channels;
    ULONG BitsPerSample;
} BAPA_CONFIGURE, *PBAPA_CONFIGURE;

/* AC97 Codec Register Offsets */
#define AC97_RESET              0x00
#define AC97_MASTER_VOLUME      0x02
#define AC97_HEADPHONE_VOLUME   0x04
#define AC97_MASTER_VOLUME_MIC  0x0C
#define AC97_LINE_OUT_VOLUME    0x18
#define AC97_MIC_VOLUME         0x0E
#define AC97_PCM_OUT_VOLUME     0x24
#define AC97_RECORD_SELECT      0x1A
#define AC97_PCM_FRONT_DAC_RATE 0x32
#define AC97_PCM_SURR_DAC_RATE  0x36
#define AC97_PCM_LFE_DAC_RATE   0x3A
#define AC97_PCM_ADC_RATE       0x3C
#define AC97_EXTENDED_STATUS    0x5C
#define AC97_POWERDOWN_CTRL     0x7A
#define AC97_VENDOR_ID1         0x7C
#define AC97_VENDOR_ID2         0x7E

/* AC97 Extended Status Register bits */
#define AC97_ESTAT_SDAC     (1 << 0)
#define AC97_ESTAT_MDAC     (1 << 1)
#define AC97_ESTAT_ADCS     (1 << 2)
#define AC97_ESTAT_DACS     (1 << 3)

/* Driver device extension */
typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;

    /* AC97 I/O resources */
    USHORT IoBase;
    USHORT IoLength;
    ULONG ChipSelect;

    /* State */
    BOOLEAN Playing;
    BOOLEAN Configured;
    BOOLEAN DeviceStarted;
    ULONG SampleRate;
    ULONG Channels;
    ULONG BitsPerSample;

    /* Write buffer for incoming PCM data */
    PUCHAR WriteBuffer;
    ULONG WriteBufferPos;
    ULONG WriteBufferSize;
    KSPIN_LOCK Lock;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* Forward declarations */
VOID Ac97Unload(PDRIVER_OBJECT DriverObject);

#endif
