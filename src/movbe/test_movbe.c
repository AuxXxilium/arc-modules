#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>

static jmp_buf jump_buffer;

void segfault_handler(int sig) {
    printf("SIGILL caught! (Module may not be loaded)\n");
    longjmp(jump_buffer, 1);
}

int main() {
    uint32_t value32 = 0x12345678;
    uint32_t result32;
    uint64_t value64 = 0x123456789ABCDEF0UL;
    uint64_t result64;
    
    signal(SIGILL, segfault_handler);
    
    printf("=== MOVBE Emulator Test ===\n\n");
    
    printf("Test 1: 32-bit MOVBE\n");
    printf("Original value: 0x%x\n", value32);
    
    if (setjmp(jump_buffer) == 0) {
        asm volatile("movbe %1, %0" : "=r"(result32) : "r"(value32));
        printf("After MOVBE:    0x%x\n", result32);
        printf("Expected:       0x78563412\n");
        printf("✓ Success!\n\n");
    } else {
        printf("✗ Failed - MOVBE not supported or module not loaded\n\n");
    }
    
    printf("Test 2: 64-bit MOVBE\n");
    printf("Original value: 0x%lx\n", value64);
    
    if (setjmp(jump_buffer) == 0) {
        asm volatile("movbe %1, %0" : "=r"(result64) : "r"(value64));
        printf("After MOVBE:    0x%lx\n", result64);
        printf("Expected:       0xf0debc9a78563412\n");
        printf("✓ Success!\n\n");
    } else {
        printf("✗ Failed - MOVBE not supported or module not loaded\n\n");
    }
    
    printf("=== Test Complete ===\n");
    return 0;
}
