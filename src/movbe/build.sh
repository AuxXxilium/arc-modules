#!/bin/bash
# Build and test the MOVBE emulator module

set -e

echo "=== Building MOVBE Emulator Module ==="
cd /tmp
make -C /lib/modules/$(uname -r)/build M=/tmp modules

echo ""
echo "=== Module built successfully ==="
echo ""
echo "To load the module, run:"
echo "  sudo insmod /tmp/movbe_emulator.ko"
echo ""
echo "To unload the module, run:"
echo "  sudo rmmod movbe_emulator"
echo ""
echo "To see kernel messages, run:"
echo "  dmesg | tail -20"
echo ""
echo "Note: This is a proof-of-concept module."
echo "A production version would include:"
echo "  - Full ModR/M byte parsing"
echo "  - Memory operand handling"
echo "  - Register operand extraction"
echo "  - Byte swapping logic"
echo "  - Proper memory access with get_user/put_user"
