/*
 * HTIF, the host interface Spike's fesvr speaks -- the output path for the
 * Spike side of the FP oracle.  Read out of the fesvr sources rather than
 * remembered: fesvr/syscall.cc's handle_syscall and dispatch.
 *
 *   tohost = <address of an 8-word block>   is a proxied system call: word 0
 *       is the number, words 1..7 the arguments, and fesvr writes the result
 *       back into word 0.  The numbers are the Linux/RISC-V ones -- write 64,
 *       exit 93 -- so they are the same ones the ecall path uses.
 *   tohost = (code << 1) | 1                is exit; bit 0 is what tells
 *       handle_syscall this is a status rather than an address, and Spike's
 *       own status is that value >> 1.
 *
 * fesvr finds `tohost' and `fromhost' by symbol name (htif.cc:179), so plain
 * aligned globals are enough -- no linker script and no .htif section.
 */
typedef unsigned long u64;

volatile u64 tohost   __attribute__((aligned(8)));
volatile u64 fromhost __attribute__((aligned(8)));

static void
htif_command(u64 payload)
{
    tohost = payload;
    /* fesvr answers by writing fromhost; a proxied syscall responds 1. */
    while (fromhost == 0)
        ;
    fromhost = 0;
}

long
rv_write(int fd, const void *buf, u64 n)
{
    /* Eight words, because dispatch() reads eight whatever the call needs. */
    static volatile u64 magic[8] __attribute__((aligned(64)));
    magic[0] = 64;                      /* SYS_write */
    magic[1] = (u64)fd;
    magic[2] = (u64)buf;
    magic[3] = n;
    magic[4] = magic[5] = magic[6] = magic[7] = 0;
    htif_command((u64)(unsigned long)magic);
    return (long)magic[0];
}

void
spike_exit(int code)
{
    tohost = ((u64)(unsigned)code << 1) | 1;
    for (;;)
        ;
}
