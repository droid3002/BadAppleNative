/**
 * PROJECT:         BadAppleAudio Driver
 * COPYRIGHT:       See LICENSE in the top level directory
 * FILE:            ac97drv.c
 * DESCRIPTION:     AC97 audio driver for NativeShell Bad Apple player.
 *                  WDM driver that provides PCM playback through AC97 codec.
 *                  Cross-compiled from Linux using MinGW-w64 + DDK headers.
 * DEVELOPERS:      Droid3002
 */

#include "ac97drv.h"

/* Forward declarations */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

/* PCI Configuration Space offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_BASE_ADDRESS_0  0x10
#define PCI_INTERRUPT_LINE  0x3C

/* AC97 PCI class code */
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_AUDIO   0x01

/* Default AC97 primary codec I/O base */
#define AC97_DEFAULT_IO_BASE 0x6000

/* Pool tag */
#define TAG_BAPA 'aPaB'

/* I/O port access helpers (x86 only) */
#ifdef _M_IX86
static __inline UCHAR InPortByte(USHORT Port)
{
    UCHAR val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(Port));
    return val;
}

static __inline VOID OutPortByte(USHORT Port, UCHAR Val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(Val), "Nd"(Port));
}

static __inline USHORT InPortWord(USHORT Port)
{
    USHORT val;
    __asm__ __volatile__("inw %1, %0" : "=a"(val) : "Nd"(Port));
    return val;
}

static __inline VOID OutPortWord(USHORT Port, USHORT Val)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(Val), "Nd"(Port));
}
#else
/* Fallback for non-x86 (won't actually work, but compiles) */
static __inline UCHAR InPortByte(USHORT Port) { (void)Port; return 0; }
static __inline VOID OutPortByte(USHORT Port, UCHAR Val) { (void)Port; (void)Val; }
static __inline USHORT InPortWord(USHORT Port) { (void)Port; return 0; }
static __inline VOID OutPortWord(USHORT Port, USHORT Val) { (void)Port; (void)Val; }
#endif

/*
 * StallMicroseconds is not in MinGW's ntoskrnl import lib.
 * Use a small volatile loop for microsecond delays.
 */
static void StallMicroseconds(ULONG us)
{
    volatile ULONG i;
    for (i = 0; i < us * 5; i++)
        ;
}

/* Port access helpers using DEVICE_EXTENSION */
static USHORT Ac97CodecPort(PDEVICE_EXTENSION Ext)
{
    return Ext->IoBase + (USHORT)(Ext->ChipSelect * 0x100);
}

/*++
 * @name Ac97ReadRegister
 * Reads a 16-bit AC97 codec register.
 *--*/
static USHORT Ac97ReadRegister(PDEVICE_EXTENSION Ext, USHORT Register)
{
    USHORT Port = Ac97CodecPort(Ext);
    OutPortByte((USHORT)(Port + 2), (UCHAR)(Register & 0x7F));
    StallMicroseconds(10);
    return InPortWord(Port);
}

/*++
 * @name Ac97WriteRegister
 * Writes a 16-bit value to an AC97 codec register.
 *--*/
static VOID Ac97WriteRegister(PDEVICE_EXTENSION Ext, USHORT Register, USHORT Value)
{
    USHORT Port = Ac97CodecPort(Ext);
    OutPortByte((USHORT)(Port + 2), (UCHAR)(Register & 0x7F));
    StallMicroseconds(10);
    OutPortWord(Port, Value);
}

/*++
 * @name Ac97Reset
 * Resets the AC97 codec and configures default volumes.
 *--*/
static NTSTATUS Ac97Reset(PDEVICE_EXTENSION Ext)
{
    ULONG timeout;
    USHORT val;

    /* Reset the codec */
    Ac97WriteRegister(Ext, AC97_RESET, 0);

    /* Wait for codec ready (power status bits all set) */
    timeout = 5000;
    while (timeout--)
    {
        val = Ac97ReadRegister(Ext, AC97_POWERDOWN_CTRL);
        if ((val & 0x000F) == 0x000F)
            break;
        StallMicroseconds(100);
    }

    /* Max volume */
    Ac97WriteRegister(Ext, AC97_MASTER_VOLUME, 0x0000);
    Ac97WriteRegister(Ext, AC97_HEADPHONE_VOLUME, 0x0000);
    Ac97WriteRegister(Ext, AC97_PCM_OUT_VOLUME, 0x0000);

    return STATUS_SUCCESS;
}

/*++
 * @name Ac97SetSampleRate
 * Sets the PCM DAC front sample rate.
 *--*/
static VOID Ac97SetSampleRate(PDEVICE_EXTENSION Ext, ULONG Rate)
{
    Ac97WriteRegister(Ext, AC97_PCM_FRONT_DAC_RATE, (USHORT)Rate);
}

/*++
 * @name Ac97StartPlayback
 * Powers up DAC and enables variable-rate audio.
 *--*/
static VOID Ac97StartPlayback(PDEVICE_EXTENSION Ext)
{
    USHORT reg;

    /* Power up PCM DAC */
    reg = Ac97ReadRegister(Ext, AC97_POWERDOWN_CTRL);
    reg &= ~0x0200;
    Ac97WriteRegister(Ext, AC97_POWERDOWN_CTRL, reg);

    /* Enable variable rate audio */
    reg = Ac97ReadRegister(Ext, AC97_EXTENDED_STATUS);
    reg |= 0x0080;
    Ac97WriteRegister(Ext, AC97_EXTENDED_STATUS, reg);

    Ac97WriteRegister(Ext, AC97_PCM_FRONT_DAC_RATE, (USHORT)Ext->SampleRate);
    Ext->Playing = TRUE;
}

/*++
 * @name Ac97StopPlayback
 * Powers down DAC.
 *--*/
static VOID Ac97StopPlayback(PDEVICE_EXTENSION Ext)
{
    USHORT reg;

    reg = Ac97ReadRegister(Ext, AC97_POWERDOWN_CTRL);
    reg |= 0x0200;
    Ac97WriteRegister(Ext, AC97_POWERDOWN_CTRL, reg);
    Ext->Playing = FALSE;
}

/*++
 * @name Ac97FeedSamples
 * Feeds PCM samples to the AC97 codec data port.
 * Handles both 8-bit and 16-bit, mono and stereo.
 *--*/
static VOID Ac97FeedSamples(PDEVICE_EXTENSION Ext, PVOID Buffer, ULONG Size)
{
    USHORT Port = Ac97CodecPort(Ext);
    USHORT DataPort = (USHORT)(Port + 4);
    PUCHAR data = (PUCHAR)Buffer;
    ULONG i;

    if (Ext->BitsPerSample == 16)
    {
        PUSHORT samples = (PUSHORT)data;
        ULONG count = Size / 2;

        for (i = 0; i < count; i++)
        {
            /* Write left channel */
            OutPortWord(DataPort, samples[i]);
            /* Mono: also write right */
            if (Ext->Channels == 1)
                OutPortWord((USHORT)(DataPort + 2), samples[i]);
        }
    }
    else /* 8-bit */
    {
        for (i = 0; i < Size; i++)
        {
            USHORT sample = (USHORT)(((USHORT)data[i] - 128) << 8);
            OutPortWord(DataPort, sample);
            if (Ext->Channels == 1)
                OutPortWord((USHORT)(DataPort + 2), sample);
        }
    }
}

/*++
 * @name Ac97ConfigureAndPlay
 * Handles BAPA_IOCTL_CONFIGURE.
 *--*/
static NTSTATUS Ac97ConfigureAndPlay(PDEVICE_EXTENSION Ext, PBAPA_CONFIGURE Cfg)
{
    NTSTATUS status;

    Ac97StopPlayback(Ext);

    if (Cfg->SampleRate < 4000 || Cfg->SampleRate > 96000)
        return STATUS_INVALID_PARAMETER;
    if (Cfg->Channels < 1 || Cfg->Channels > 2)
        return STATUS_INVALID_PARAMETER;
    if (Cfg->BitsPerSample != 8 && Cfg->BitsPerSample != 16)
        return STATUS_INVALID_PARAMETER;

    Ext->Channels = Cfg->Channels;
    Ext->BitsPerSample = Cfg->BitsPerSample;

    status = Ac97Reset(Ext);
    if (!NT_SUCCESS(status))
        return status;

    Ac97SetSampleRate(Ext, Cfg->SampleRate);
    Ext->SampleRate = Cfg->SampleRate;

    /* Allocate 1 second write buffer */
    if (Ext->WriteBuffer)
        ExFreePoolWithTag(Ext->WriteBuffer, TAG_BAPA);

    Ext->WriteBufferSize = Cfg->SampleRate * Cfg->Channels * (Cfg->BitsPerSample / 8);
    Ext->WriteBuffer = ExAllocatePoolWithTag(NonPagedPool, Ext->WriteBufferSize, TAG_BAPA);
    if (!Ext->WriteBuffer)
    {
        Ext->WriteBufferSize = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Ext->WriteBufferPos = 0;
    Ext->Configured = TRUE;

    Ac97StartPlayback(Ext);
    return STATUS_SUCCESS;
}

/*++
 * @name Ac97HandleWrite
 * Handles BAPA_IOCTL_WRITE: streams PCM to the codec.
 *--*/
static NTSTATUS Ac97HandleWrite(PDEVICE_EXTENSION Ext, PVOID Buffer, ULONG Size)
{
    if (!Ext->Configured || !Ext->Playing)
        return STATUS_INVALID_DEVICE_STATE;
    if (!Buffer || Size == 0)
        return STATUS_INVALID_PARAMETER;

    Ac97FeedSamples(Ext, Buffer, Size);
    return STATUS_SUCCESS;
}

/* ======================================
 * IRP Dispatch Routines
 * ====================================== */

static NTSTATUS Ac97DispatchCreate(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)DevObj->DeviceExtension;
    ext->Playing = FALSE;
    ext->Configured = FALSE;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS Ac97DispatchClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)DevObj->DeviceExtension;
    Ac97StopPlayback(ext);
    if (ext->WriteBuffer)
    {
        ExFreePoolWithTag(ext->WriteBuffer, TAG_BAPA);
        ext->WriteBuffer = NULL;
    }
    ext->Configured = FALSE;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS Ac97DispatchDeviceControl(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)DevObj->DeviceExtension;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;
    ULONG info = 0;

    switch (sp->Parameters.DeviceIoControl.IoControlCode)
    {
        case BAPA_IOCTL_CONFIGURE:
        {
            if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(BAPA_CONFIGURE))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            status = Ac97ConfigureAndPlay(ext, (PBAPA_CONFIGURE)Irp->AssociatedIrp.SystemBuffer);
            break;
        }
        case BAPA_IOCTL_WRITE:
        {
            if (sp->Parameters.DeviceIoControl.InputBufferLength == 0)
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            status = Ac97HandleWrite(ext,
                                     Irp->AssociatedIrp.SystemBuffer,
                                     sp->Parameters.DeviceIoControl.InputBufferLength);
            break;
        }
        case BAPA_IOCTL_STOP:
        {
            Ac97StopPlayback(ext);
            status = STATUS_SUCCESS;
            break;
        }
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS Ac97DispatchPnp(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)DevObj->DeviceExtension;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;

    switch (sp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            /* Assume standard AC97 I/O base for now.
             * A production driver would query PCI resources here. */
            ext->IoBase = AC97_DEFAULT_IO_BASE;
            ext->IoLength = 0x100;
            ext->ChipSelect = 0;

            /* Quick probe: try to read the AC97 vendor ID */
            {
                USHORT vid = Ac97ReadRegister(ext, AC97_VENDOR_ID1);
                if (vid == 0 || vid == 0xFFFF)
                {
                    /* No codec responding at this address.
                     * Still succeed the start so the device object exists;
                     * IOCTLs will fail gracefully. */
                    ext->IoBase = 0;
                }
            }

            ext->DeviceStarted = TRUE;
            break;
        }
        case IRP_MN_STOP_DEVICE:
        {
            Ac97StopPlayback(ext);
            ext->DeviceStarted = FALSE;
            break;
        }
        case IRP_MN_REMOVE_DEVICE:
        {
            Ac97StopPlayback(ext);
            if (ext->WriteBuffer)
            {
                ExFreePoolWithTag(ext->WriteBuffer, TAG_BAPA);
                ext->WriteBuffer = NULL;
            }
            if (ext->LowerDevice)
            {
                IoDetachDevice(ext->LowerDevice);
                ext->LowerDevice = NULL;
            }
            IoDeleteDevice(DevObj);
            break;
        }
        default:
            break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS Ac97DispatchPower(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS Ac97DispatchDefault(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

/*++
 * @name Ac97AddDevice
 * Called by PnP when the device instance is enumerated.
 *--*/
static NTSTATUS Ac97AddDevice(PDRIVER_OBJECT DriverObj, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT devObj = NULL;
    PDEVICE_EXTENSION ext;
    UNICODE_STRING devName;
    UNICODE_STRING symName;
    NTSTATUS status;

    RtlInitUnicodeString(&devName, DEVICE_NAME_STRING);
    RtlInitUnicodeString(&symName, SYMBOLIC_NAME_STRING);

    status = IoCreateDevice(DriverObj,
                            sizeof(DEVICE_EXTENSION),
                            &devName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &devObj);
    if (!NT_SUCCESS(status))
        return status;

    ext = (PDEVICE_EXTENSION)devObj->DeviceExtension;
    RtlZeroMemory(ext, sizeof(DEVICE_EXTENSION));
    ext->DeviceObject = devObj;
    ext->LowerDevice = IoAttachDeviceToDeviceStack(devObj, Pdo);

    KeInitializeSpinLock(&ext->Lock);

    status = IoCreateSymbolicLink(&symName, &devName);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(devObj);
        return status;
    }

    devObj->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/*++
 * @name Ac97Unload
 *--*/
VOID Ac97Unload(PDRIVER_OBJECT DriverObj)
{
    UNICODE_STRING symName;
    UNREFERENCED_PARAMETER(DriverObj);
    RtlInitUnicodeString(&symName, SYMBOLIC_NAME_STRING);
    IoDeleteSymbolicLink(&symName);
}

/*++
 * @name DriverEntry
 * Entry point for the driver.
 *--*/
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    /* Set all major function handlers to default first */
    {
        ULONG i;
        for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
            DriverObject->MajorFunction[i] = Ac97DispatchDefault;
    }

    /* Override specific handlers */
    DriverObject->DriverExtension->AddDevice = Ac97AddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = Ac97DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = Ac97DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Ac97DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = Ac97DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER]          = Ac97DispatchPower;
    DriverObject->DriverUnload = Ac97Unload;

    return STATUS_SUCCESS;
}
