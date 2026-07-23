#!/bin/bash
# setup_vm.sh - Create a Windows VM in VirtualBox and prepare it for NativeShell
#
# Usage: ./setup_vm.sh [path_to_windows_iso]

set -e

VM_NAME="NativeShell-Test"
VM_DIR="$HOME/VirtualBox VMs/$VM_NAME"
ISO_PATH="${1:-}"
DISK_SIZE=20480  # 20GB

echo "============================================"
echo "  NativeShell VM Setup"
echo "============================================"

# Check VBoxManage
if ! command -v VBoxManage &>/dev/null; then
    echo "ERROR: VBoxManage not found. Install VirtualBox first."
    exit 1
fi

# Check if VM already exists
if VBoxManage showvminfo "$VM_NAME" &>/dev/null; then
    echo "VM '$VM_NAME' already exists."
    echo "  Start it:  VBoxManage startvm '$VM_NAME'"
    echo "  Delete it: VBoxManage unregistervm '$VM_NAME' --delete"
    exit 0
fi

# Create VM
echo ""
echo "[1/5] Creating VM..."
VBoxManage createvm --name "$VM_NAME" --ostype WindowsXP --register
VBoxManage modifyvm "$VM_NAME" --memory 512 --vram 16 --cpus 1
VBoxManage modifyvm "$VM_NAME" --acpi on --ioapic on
VBoxManage modifyvm "$VM_NAME" --boot1 dvd --boot2 disk --boot3 none --boot4 none
VBoxManage modifyvm "$VM_NAME" --graphicscontroller vboxvga
VBoxManage modifyvm "$VM_NAME" --audio-driver null

# Create virtual disk
echo "[2/5] Creating virtual disk (${DISK_SIZE}MB)..."
VBoxManage createmedium disk --filename "$VM_DIR/$VM_NAME.vdi" --size $DISK_SIZE --format VDI

# Create IDE controller and attach disk
echo "[3/5] Attaching disk..."
VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4
VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 \
    --type hdd --medium "$VM_DIR/$VM_NAME.vdi"

# Attach ISO if provided
if [ -n "$ISO_PATH" ] && [ -f "$ISO_PATH" ]; then
    echo "[4/5] Attaching Windows ISO: $ISO_PATH"
    VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 1 \
        --type dvddrive --medium "$ISO_PATH"
else
    echo "[4/5] No ISO provided. Attach Windows ISO manually before starting."
    echo "  VBoxManage storageattach '$VM_NAME' --storagectl 'IDE' --port 0 --device 1 --type dvddrive --medium /path/to/windows.iso"
fi

# Enable shared folder for file transfer
echo "[5/5] Setting up shared folder..."
mkdir -p /tmp/nativeshell_share
cp -f NativeShell/native.exe /tmp/nativeshell_share/
cp -f NativeShell/badapple.dat /tmp/nativeshell_share/
cp -f NativeShell/badapple_audio.dat /tmp/nativeshell_share/
cp -f ac97drv/BadAppleAudio.sys /tmp/nativeshell_share/
cp -f launcher.exe /tmp/nativeshell_share/ 2>/dev/null || true

VBoxManage sharedfolder add "$VM_NAME" --name "nativeshell" \
    --hostpath /tmp/nativeshell_share --automount

echo ""
echo "============================================"
echo "  VM '$VM_NAME' created!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Start the VM:"
echo "     VBoxManage startvm '$VM_NAME'"
echo ""
echo "  2. Install Windows from the ISO"
echo ""
echo "  3. After Windows is installed, install VirtualBox Guest Additions"
echo "     (Devices > Insert Guest Additions CD)"
echo ""
echo "  4. The files are in the shared folder at:"
echo "     \\\\VBOXSRV\\nativeshell\\"
echo "     Copy them to C:\\NativeShell\\"
echo ""
echo "  5. To run, see the instructions in RUN.txt"
