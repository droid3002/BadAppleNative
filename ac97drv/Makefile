# Makefile for BadAppleAudio - Cross-compiled from Linux using MinGW-w64
#
# Produces a Windows kernel-mode driver (.sys) from Linux.
# Uses MinGW-w64 DDK headers and ntoskrnl import library.
#
# Usage: make

CC = i686-w64-mingw32-gcc
STRIP = i686-w64-mingw32-strip

# Kernel-mode driver flags:
# -fno-builtin   : Don't use compiler builtins
# -nostdlib      : No CRT
# -shared        : Build as DLL (kernel drivers are loaded like DLLs)
# -Wl,--entry    : Entry point = DriverEntry
CFLAGS = -O2 -w -fno-builtin -nostdlib \
         -ffreestanding -I/usr/i686-w64-mingw32/include/ddk

LDFLAGS = -shared -nostdlib -Wl,--entry,_DriverEntry@8 \
          -Wl,--subsystem,native \
          -lntoskrnl -lgcc

TARGET = BadAppleAudio.sys

all: $(TARGET)

$(TARGET): ac97drv.o
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	$(STRIP) $@
	@echo "Driver build complete: $(TARGET)"
	@ls -lh $@

ac97drv.o: ac97drv.c ac97drv.h
	$(CC) $(CFLAGS) -c -o $@ ac97drv.c

clean:
	rm -f ac97drv.o $(TARGET)

.PHONY: all clean
