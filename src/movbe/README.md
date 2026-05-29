# MOVBE Emulator Kernel Module

This Linux kernel module intercepts illegal instruction exceptions and emulates `movbe` (Move Big Endian) instructions using basic x86 operations.

## How it works

1. **Registers an exception handler** via `register_die_notifier()` to catch SIGILL (illegal instruction) traps
2. **Decodes the instruction** at the faulting address to identify if it's a MOVBE instruction
3. **Emulates the instruction** by:
   - Extracting operands from the ModR/M byte
   - Performing byte-swapping using bit manipulation
   - Updating registers/memory with swapped values
4. **Resumes execution** at the next instruction

## Building

### Prerequisites
```bash
sudo apt-get install linux-headers-$(uname -r)
sudo apt-get install build-essential
```

### Compile
```bash
cd /tmp
make -C /lib/modules/$(uname -r)/build M=/tmp modules
```

### Output
```
movbe_emulator.ko  - The compiled kernel module
movbe_emulator.o   - Object file
movbe_emulator.mod.* - Module metadata
```

## Usage

### Load the module
```bash
sudo insmod movbe_emulator.ko
```

### Verify it's loaded
```bash
lsmod | grep movbe
```

### View kernel messages
```bash
dmesg | tail -20
```

### Unload the module
```bash
sudo rmmod movbe_emulator
```

## Testing

You can test with code that uses `movbe`. For example:
```c
#include <stdint.h>
#include <stdio.h>

int main() {
    uint32_t value = 0x12345678;
    uint32_t swapped;
    
    // This will trigger SIGILL on CPUs without MOVBE support
    asm volatile("movbe %1, %0" : "=r"(swapped) : "r"(value));
    
    printf("Original: 0x%x\n", value);
    printf("Swapped:  0x%x\n", swapped);
    
    return 0;
}
```

Compile with CPU flags that include movbe:
```bash
gcc -march=native -O2 test.c -o test
./test  # With module loaded, this should work even without native MOVBE support
```

## Important Notes

- **Performance**: Exception-based emulation is **very slow** - each MOVBE instruction causes a context switch to kernel mode
- **Incomplete**: This is a proof-of-concept. Full production implementation would need:
  - Complete ModR/M decoding for all addressing modes
  - SIB byte parsing (for complex addressing)
  - Proper memory access using `get_user()` / `put_user()`
  - 16-bit operand support
  - REX prefix handling
  - Error handling for invalid memory accesses
  - Thread safety considerations
  
- **Limitations**: 
  - Only catches SIGILL exceptions
  - Doesn't work with direct instruction patching
  - May interfere with other SIGILL handlers
  - Kernel memory access requires CAP_SYS_ADMIN

## Architecture

```
User Program
    ↓
Executes MOVBE (unsupported instruction)
    ↓
CPU raises #UD (Undefined Opcode) exception
    ↓
Kernel handles as SIGILL (trap 6)
    ↓
Die notifier chain invoked
    ↓
movbe_handler() called
    ↓
Instruction decoded and emulated
    ↓
Registers/memory updated
    ↓
Execution resumed at next instruction
```

## Files

- `movbe_emulator.c` - Simplified version for reference
- `movbe_emulator_full.c` - More complete version with helper functions
- `Makefile` - Build configuration

## Alternatives

For production use, consider:
1. **Binary rewriting**: QEMU user-mode, DynamoRIO, Pin
2. **Recompilation**: `-march=x86-64-v2` or lower to avoid MOVBE
3. **CPU microcode update**: Some CPUs can enable MOVBE via firmware
4. **Virtualization**: Run in QEMU which handles the translation
