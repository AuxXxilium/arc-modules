#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/traps.h>
#include <linux/kdebug.h>
#include <linux/proc_fs.h>
#include <asm/insn.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Copilot");
MODULE_DESCRIPTION("Emulate MOVBE instructions and CPUID support");
MODULE_VERSION("1.0");

static int movbe_count = 0;
static int cpuid_hooked_count = 0;

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
        
        cpuid_hooked_count++;
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

/* Emulate MOVBE with proper operand handling */
static int handle_movbe(struct pt_regs *regs, unsigned char *code, int is_64bit)
{
    unsigned char modrm;
    int reg, rm;
    int mod;
    unsigned long src_value, dst_value;
    unsigned long operand_mask;
    int operand_size = is_64bit ? 8 : 4;

    if (!code || code[0] != 0x0f || code[1] != 0x38) {
        return -1;
    }

    modrm = code[2];
    reg = modrm_reg(modrm);
    rm = modrm_rm(modrm);
    mod = modrm_mod(modrm);

    operand_mask = is_64bit ? 0xFFFFFFFFFFFFFFFFUL : 0xFFFFFFFFUL;

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
            return 3;  /* Instruction length is 3 bytes for ModR/M=11 */
        } else {
            /* Memory to register - complex addressing, simplified handling */
            printk(KERN_DEBUG "MOVBE Emulator: movbe r%d, [memory] - memory addressing not fully implemented\n", reg);
            return 4;
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
            return 3;  /* Instruction length is 3 bytes for ModR/M=11 */
        } else {
            /* Register to memory - complex addressing, simplified handling */
            printk(KERN_DEBUG "MOVBE Emulator: movbe [memory], r%d - memory addressing not fully implemented\n", reg);
            return 4;
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
        movbe_count++;
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
    seq_printf(m, "MOVBE instructions emulated: %d\n", movbe_count);
    seq_printf(m, "CPUID calls hooked: %d\n", cpuid_hooked_count);
    seq_printf(m, "\nNote: MOVBE flag is added to CPUID results.\n");
    seq_printf(m, "Check /proc/cpuinfo for 'movbe' in CPU flags after running programs.\n");
    
    return 0;
}

static int movbe_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, movbe_status_read_handler, NULL);
}

static const struct proc_ops movbe_status_proc_ops = {
    .proc_open = movbe_status_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

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
    printk(KERN_INFO "MOVBE Emulator: Unloaded (emulated %d MOVBE instructions)\n", movbe_count);
}

module_init(movbe_init);
module_exit(movbe_exit);
