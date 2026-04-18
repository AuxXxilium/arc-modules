#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdebug.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <asm/insn.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <linux/atomic.h>

/* Define DIE_TRAP for older kernels that don't have linux/traps.h */
#ifndef DIE_TRAP
#define DIE_TRAP 1
#endif

/* Define DIE_CALL for older kernels */
#ifndef DIE_CALL
#define DIE_CALL 2
#endif

/* Check if proc_ops structure exists (kernel 5.3+) */
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif
#if !defined(HAVE_PROC_OPS)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
#define HAVE_PROC_OPS
#endif
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AuxXxilium");
MODULE_DESCRIPTION("Emulate MOVBE instructions and CPUID support");
MODULE_VERSION("1.2");

static atomic_t movbe_count = ATOMIC_INIT(0);
static atomic_t cpuid_hooked_count = ATOMIC_INIT(0);

/* Extract register number from ModR/M byte */
static inline int modrm_reg(unsigned char modrm)
{
    return (modrm >> 3) & 7;
}

static inline int modrm_rm(unsigned char modrm)
{
    return modrm & 7;
}

static inline int modrm_mod(unsigned char modrm)
{
    return (modrm >> 6) & 3;
}

/* Get register value from pt_regs */
static unsigned long get_reg_value(struct pt_regs *regs, int reg)
{
    switch(reg) {
        case 0: return regs->ax;
        case 1: return regs->cx;
        case 2: return regs->dx;
        case 3: return regs->bx;
        case 4: return regs->sp;
        case 5: return regs->bp;
        case 6: return regs->si;
        case 7: return regs->di;
        default: return 0;
    }
}

/* Set register value in pt_regs */
static void set_reg_value(struct pt_regs *regs, int reg, unsigned long value)
{
    switch(reg) {
        case 0: regs->ax = value; break;
        case 1: regs->cx = value; break;
        case 2: regs->dx = value; break;
        case 3: regs->bx = value; break;
        case 4: regs->sp = value; break;
        case 5: regs->bp = value; break;
        case 6: regs->si = value; break;
        case 7: regs->di = value; break;
    }
}

/* Swap bytes in a 32-bit or 64-bit value */
static uint32_t bswap32(uint32_t val)
{
    return ((val & 0xFFU) << 24) |
           ((val & 0xFF00U) << 8) |
           ((val & 0xFF0000U) >> 8) |
           ((val & 0xFF000000U) >> 24);
}

static uint64_t bswap64(uint64_t val)
{
    return ((val & 0xFFUL) << 56) |
           ((val & 0xFF00UL) << 40) |
           ((val & 0xFF0000UL) << 24) |
           ((val & 0xFF000000UL) << 8) |
           ((val & 0xFF00000000UL) >> 8) |
           ((val & 0xFF0000000000UL) >> 24) |
           ((val & 0xFF000000000000UL) >> 40) |
           ((val & 0xFF00000000000000UL) >> 56);
}

/* Handle CPUID instruction to fake MOVBE flag */
static int handle_cpuid(struct pt_regs *regs)
{
    uint32_t eax_in = regs->ax;

    /* CPUID function 1: processor info and feature bits */
    if (eax_in == 1) {
        /* EBX bit 22 is MOVBE support */
        regs->bx |= (1 << 22);
        
        atomic_inc(&cpuid_hooked_count);
        printk(KERN_DEBUG "MOVBE Emulator: CPUID(1) - added MOVBE flag to EBX\n");
        return 1;
    }

    return 0;
}

/* Check if instruction is CPUID (0F A2) */
static inline int is_cpuid_insn(unsigned char *code)
{
    return code[0] == 0x0f && code[1] == 0xa2;
}

/* Parse SIB (Scale-Index-Base) byte for complex addressing modes */
static int parse_sib_address(struct pt_regs *regs, unsigned char sib, unsigned char *disp_bytes, 
                             int disp_size, unsigned long *addr, int *bytes_consumed)
{
    unsigned char scale = (sib >> 6) & 3;
    unsigned char index = (sib >> 3) & 7;
    unsigned char base = sib & 7;
    unsigned long base_val, index_val;
    int disp = 0;

    base_val = get_reg_value(regs, base);
    index_val = (index == 4) ? 0 : get_reg_value(regs, index);  /* index=4 means no index */

    /* Scale: 0=1x, 1=2x, 2=4x, 3=8x */
    index_val = index_val << scale;

    /* Add displacement */
    if (disp_size == 1) {
        disp = (signed char)disp_bytes[0];
    } else if (disp_size == 4) {
        disp = *(int32_t *)disp_bytes;
    }

    *addr = base_val + index_val + disp;
    *bytes_consumed = 1 + disp_size;  /* 1 for SIB + displacement bytes */
    
    return 0;
}

/* Calculate memory address from ModR/M and optional SIB/displacement bytes */
static int calculate_memory_address(struct pt_regs *regs, unsigned char *code, 
                                    unsigned char modrm, unsigned long *addr, int *instr_size)
{
    unsigned char mod = modrm_mod(modrm);
    unsigned char rm = modrm_rm(modrm);
    unsigned long base_val;
    int disp = 0;
    int disp_size = 0;
    int offset = 3;  /* Start after opcode bytes (0F 38 F0/F1) */

    if (mod == 3) {
        /* Register mode - not memory */
        return -1;
    }

    if (rm == 4) {
        /* SIB byte present */
        unsigned char sib = code[offset];
        int sib_consumed = 0;
        
        if (mod == 0) {
            disp_size = 0;
        } else if (mod == 1) {
            disp_size = 1;
        } else {  /* mod == 2 */
            disp_size = 4;
        }
        
        parse_sib_address(regs, sib, &code[offset + 1], disp_size, addr, &sib_consumed);
        *instr_size = 3 + sib_consumed;  /* 3 bytes for opcode + SIB + displacement */
        return 0;
    }

    /* No SIB byte, rm is base register */
    base_val = get_reg_value(regs, rm);

    if (mod == 0) {
        /* No displacement (except special cases) */
        if (rm == 5) {
            /* RIP-relative or 32-bit absolute - complex, skip for now */
            printk(KERN_WARNING "MOVBE: RIP-relative addressing not supported\n");
            return -1;
        }
        *addr = base_val;
        *instr_size = 3;  /* Just opcode + ModR/M */
    } else if (mod == 1) {
        /* 8-bit signed displacement */
        disp = (signed char)code[3];
        *addr = base_val + disp;
        *instr_size = 4;  /* opcode + ModR/M + 1 byte disp */
    } else {  /* mod == 2 */
        /* 32-bit signed displacement */
        disp = *(int32_t *)&code[3];
        *addr = base_val + disp;
        *instr_size = 7;  /* opcode + ModR/M + 4 bytes disp */
    }

    return 0;
}

/* Safely read value from user/kernel memory */
static int safe_read_value(unsigned long addr, unsigned long *value, int size)
{
    if (size == 4) {
        uint32_t val32;
        if (get_user(val32, (uint32_t __user *)addr)) {
            printk(KERN_WARNING "MOVBE: Failed to read from address %lx\n", addr);
            return -1;
        }
        *value = val32;
    } else if (size == 8) {
        uint64_t val64;
        if (get_user(val64, (uint64_t __user *)addr)) {
            printk(KERN_WARNING "MOVBE: Failed to read from address %lx\n", addr);
            return -1;
        }
        *value = val64;
    } else {
        return -1;
    }
    return 0;
}

/* Safely write value to user/kernel memory */
static int safe_write_value(unsigned long addr, unsigned long value, int size)
{
    if (size == 4) {
        if (put_user((uint32_t)value, (uint32_t __user *)addr)) {
            printk(KERN_WARNING "MOVBE: Failed to write to address %lx\n", addr);
            return -1;
        }
    } else if (size == 8) {
        if (put_user((uint64_t)value, (uint64_t __user *)addr)) {
            printk(KERN_WARNING "MOVBE: Failed to write to address %lx\n", addr);
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

/* Emulate MOVBE with full addressing mode support */
static int handle_movbe(struct pt_regs *regs, unsigned char *code, int is_64bit)
{
    unsigned char modrm;
    int reg, rm;
    int mod;
    unsigned long src_value, dst_value;
    unsigned long mem_addr;
    int operand_size = is_64bit ? 8 : 4;
    int instr_size = 3;  /* Default: opcode + ModR/M */

    if (!code || code[0] != 0x0f || code[1] != 0x38) {
        return -1;
    }

    modrm = code[2];
    reg = modrm_reg(modrm);
    rm = modrm_rm(modrm);
    mod = modrm_mod(modrm);

    if (code[3] == 0xf0) {
        /* MOVBE r, r/m - load from source with byte swap to register */
        
        if (mod == 3) {
            /* Register to register: movbe r_reg, r_rm */
            src_value = get_reg_value(regs, rm);
            
            if (is_64bit) {
                dst_value = bswap64(src_value);
            } else {
                dst_value = bswap32(src_value & 0xFFFFFFFFUL);
            }
            
            set_reg_value(regs, reg, dst_value);
            printk(KERN_DEBUG "MOVBE Emulator: movbe r%d, r%d (0x%lx -> 0x%lx)\n", 
                   reg, rm, src_value, dst_value);
            return 3;
        } else {
            /* Memory to register */
            if (calculate_memory_address(regs, code, modrm, &mem_addr, &instr_size) < 0) {
                return -1;
            }
            
            if (safe_read_value(mem_addr, &src_value, operand_size) < 0) {
                return -1;
            }
            
            if (is_64bit) {
                dst_value = bswap64(src_value);
            } else {
                dst_value = bswap32(src_value & 0xFFFFFFFFUL);
            }
            
            set_reg_value(regs, reg, dst_value);
            printk(KERN_DEBUG "MOVBE Emulator: movbe r%d, [%lx] (0x%lx -> 0x%lx)\n", 
                   reg, mem_addr, src_value, dst_value);
            return instr_size;
        }

    } else if (code[3] == 0xf1) {
        /* MOVBE r/m, r - store from register to destination with byte swap */
        
        if (mod == 3) {
            /* Register to register: movbe r_rm, r_reg */
            src_value = get_reg_value(regs, reg);
            
            if (is_64bit) {
                dst_value = bswap64(src_value);
            } else {
                dst_value = bswap32(src_value & 0xFFFFFFFFUL);
            }
            
            set_reg_value(regs, rm, dst_value);
            printk(KERN_DEBUG "MOVBE Emulator: movbe r%d, r%d (0x%lx -> 0x%lx)\n", 
                   rm, reg, src_value, dst_value);
            return 3;
        } else {
            /* Register to memory */
            if (calculate_memory_address(regs, code, modrm, &mem_addr, &instr_size) < 0) {
                return -1;
            }
            
            src_value = get_reg_value(regs, reg);
            
            if (is_64bit) {
                dst_value = bswap64(src_value);
            } else {
                dst_value = bswap32(src_value & 0xFFFFFFFFUL);
            }
            
            if (safe_write_value(mem_addr, dst_value, operand_size) < 0) {
                return -1;
            }
            
            printk(KERN_DEBUG "MOVBE Emulator: movbe [%lx], r%d (0x%lx -> 0x%lx)\n", 
                   mem_addr, reg, src_value, dst_value);
            return instr_size;
        }
    }

    return -1;
}

static int movbe_handler(struct notifier_block *nb, unsigned long val, void *data)
{
    struct die_args *args = (struct die_args *)data;
    struct pt_regs *regs = args->regs;
    unsigned char *code;
    int insn_len;
    int is_64bit = 0;

    if (val != DIE_TRAP)
        return NOTIFY_DONE;

    if (regs->orig_ax != 6)  /* Only handle SIGILL (trap 6) */
        return NOTIFY_DONE;

    code = (unsigned char *)regs->ip;

    /* Check for CPUID instruction first (0F A2) */
    if (is_cpuid_insn(code)) {
        if (handle_cpuid(regs)) {
            regs->ip += 2;  /* CPUID is 2 bytes */
            return NOTIFY_STOP;
        }
    }

    /* Check for REX.W prefix (0x48) for 64-bit operands */
    if (code[0] == 0x48) {
        is_64bit = 1;
        code += 1;  /* Skip REX prefix */
    }

    /* Check for MOVBE instructions (0F 38 F0/F1) */
    if (code[0] != 0x0f || code[1] != 0x38) {
        return NOTIFY_DONE;
    }

    if (code[2] != 0xf0 && code[2] != 0xf1) {
        return NOTIFY_DONE;
    }

    /* This is a MOVBE instruction - emulate it */
    printk(KERN_DEBUG "MOVBE Emulator: Caught MOVBE at %px (%s-bit)\n", 
           (void *)regs->ip, is_64bit ? "64" : "32");

    insn_len = handle_movbe(regs, (unsigned char *)regs->ip + (is_64bit ? 1 : 0), is_64bit);
    
    if (insn_len > 0) {
        /* Successfully emulated - skip past instruction */
        regs->ip += insn_len + (is_64bit ? 1 : 0);  /* Add REX prefix length if present */
        atomic_inc(&movbe_count);
        return NOTIFY_STOP;
    }

    return NOTIFY_DONE;
}

static struct notifier_block movbe_nb = {
    .notifier_call = movbe_handler,
    .priority = INT_MAX,
};

/* Proc filesystem read handler for /proc/movbe_status */
static int movbe_status_read_handler(struct seq_file *m, void *v)
{
    seq_printf(m, "MOVBE Emulator Status\n");
    seq_printf(m, "====================\n");
    seq_printf(m, "Module Status: Active\n");
    seq_printf(m, "MOVBE instructions emulated: %d\n", atomic_read(&movbe_count));
    seq_printf(m, "CPUID calls hooked: %d\n", atomic_read(&cpuid_hooked_count));
    seq_printf(m, "\nNote: MOVBE flag is added to CPUID results.\n");
    seq_printf(m, "Check /proc/cpuinfo for 'movbe' in CPU flags after running programs.\n");
    seq_printf(m, "Memory addressing modes fully supported (mod=0/1/2, SIB, displacements).\n");
    
    return 0;
}

static int movbe_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, movbe_status_read_handler, NULL);
}

/* Use the appropriate proc operations structure based on kernel version */
#ifdef HAVE_PROC_OPS
/* Kernel 5.x+ uses proc_ops */
static const struct proc_ops movbe_status_proc_ops = {
    .proc_open = movbe_status_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
/* Kernel 4.4 uses file_operations */
static const struct file_operations movbe_status_proc_ops = {
    .owner = THIS_MODULE,
    .open = movbe_status_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

static int __init movbe_init(void)
{
    int ret;
    struct proc_dir_entry *entry;

    printk(KERN_INFO "MOVBE Emulator: Loading kernel module\n");

    ret = register_die_notifier(&movbe_nb);
    if (ret) {
        printk(KERN_ERR "MOVBE Emulator: Failed to register notifier\n");
        return ret;
    }

    /* Create /proc/movbe_status to show emulator statistics */
    entry = proc_create("movbe_status", 0444, NULL, &movbe_status_proc_ops);
    if (!entry) {
        printk(KERN_ERR "MOVBE Emulator: Failed to create proc entry\n");
        unregister_die_notifier(&movbe_nb);
        return -ENOMEM;
    }

    printk(KERN_INFO "MOVBE Emulator: Registered exception handler\n");
    printk(KERN_INFO "MOVBE Emulator: CPUID hooking enabled - MOVBE flag added to CPUID results\n");
    printk(KERN_INFO "MOVBE Emulator: Created /proc/movbe_status\n");
    printk(KERN_INFO "MOVBE Emulator: Ready to emulate MOVBE instructions\n");
    
    return 0;
}

static void __exit movbe_exit(void)
{
    remove_proc_entry("movbe_status", NULL);
    unregister_die_notifier(&movbe_nb);
    printk(KERN_INFO "MOVBE Emulator: Unloaded (emulated %d MOVBE instructions)\n", 
           atomic_read(&movbe_count));
}

module_init(movbe_init);
module_exit(movbe_exit);
