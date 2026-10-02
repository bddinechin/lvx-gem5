/*
 * Zicsr and Zifencei for the LVX ISS's RV64G personality.
 *
 * The RISC-V csrrw/csrrs/csrrc here are the NATIVE LVX instructions under a
 * second encoding, not a separate implementation: they share the BitAlias
 * dispatch that maps fcsr/fflags/frm onto CS and mepc/mtvec/mtval/mscratch
 * onto SPC/EV/EA/SR.  So this test is as much about that sharing working as
 * about the encoding decoding.
 *
 * Two things it is really looking for.  `csrw csr, rs' is `csrrw x0, csr, rs'
 * -- the commonest way to write a CSR discards the result -- and the shared
 * dispatch commits unconditionally, so if x0 is not hardwired zero this
 * clobbers it and every later use of a zero operand is wrong.  And the
 * immediate forms take a 5-bit zero-extended value, so a sign extension there
 * turns a set of bit 4 into a set of everything.
 *
 * One check per group, each returning its own number; 0 means all passed.
 */
typedef unsigned long u64;

long rv_write(int fd, const void *buf, unsigned long n);

#define csrrw(csr, v)  ({ u64 _o; __asm__ volatile ("csrrw %0, " #csr ", %1" \
                          : "=r"(_o) : "r"((u64)(v))); _o; })
#define csrrs(csr, v)  ({ u64 _o; __asm__ volatile ("csrrs %0, " #csr ", %1" \
                          : "=r"(_o) : "r"((u64)(v))); _o; })
#define csrrc(csr, v)  ({ u64 _o; __asm__ volatile ("csrrc %0, " #csr ", %1" \
                          : "=r"(_o) : "r"((u64)(v))); _o; })
#define csrr(csr)      ({ u64 _o; __asm__ volatile ("csrr %0, " #csr \
                          : "=r"(_o)); _o; })
#define csrw(csr, v)   __asm__ volatile ("csrw " #csr ", %0" :: "r"((u64)(v)))
#define csrrwi(csr, i) ({ u64 _o; __asm__ volatile ("csrrwi %0, " #csr ", " #i \
                          : "=r"(_o)); _o; })
#define csrrsi(csr, i) ({ u64 _o; __asm__ volatile ("csrrsi %0, " #csr ", " #i \
                          : "=r"(_o)); _o; })
#define csrrci(csr, i) ({ u64 _o; __asm__ volatile ("csrrci %0, " #csr ", " #i \
                          : "=r"(_o)); _o; })

int
main(void)
{
    /* 1: fcsr read/write, and that it reads back what was written. */
    {
        csrrw(fcsr, 0);
        if (csrrw(fcsr, 0xff) != 0) return 1;       /* old value, cleared above */
        if (csrr(fcsr) != 0xff) return 1;           /* 5 flag bits + 3 rm bits */
        csrrw(fcsr, 0);
        if (csrr(fcsr) != 0) return 1;
    }

    /* 2: fflags and frm are narrowings of the same register -- fflags is
     *    fcsr[4:0], frm is fcsr[7:5] right-aligned. */
    {
        csrrw(fcsr, 0);
        csrrw(fflags, 0x1f);
        csrrw(frm, 7);
        if (csrr(fflags) != 0x1f) return 2;
        if (csrr(frm) != 7) return 2;
        if (csrr(fcsr) != 0xff) return 2;           /* the two halves together */
        csrrw(fcsr, 0);
    }

    /* 3: set and clear, which read the old value and then modify it. */
    {
        csrrw(fflags, 0);
        if (csrrs(fflags, 0x11) != 0) return 3;
        if (csrr(fflags) != 0x11) return 3;
        if (csrrs(fflags, 0x04) != 0x11) return 3;  /* or-ed in */
        if (csrr(fflags) != 0x15) return 3;
        if (csrrc(fflags, 0x04) != 0x15) return 3;  /* and-not-ed out */
        if (csrr(fflags) != 0x11) return 3;
        csrrw(fflags, 0);
    }

    /* 4: the immediate forms, including a value with bit 4 set -- which a
     *    sign-extended 5-bit immediate would turn into all ones. */
    {
        csrrw(fflags, 0);
        if (csrrwi(fflags, 16) != 0) return 4;
        if (csrr(fflags) != 16) return 4;           /* not 0xfff...f0 */
        if (csrrsi(fflags, 1) != 16) return 4;
        if (csrr(fflags) != 17) return 4;
        if (csrrci(fflags, 16) != 17) return 4;
        if (csrr(fflags) != 1) return 4;
        csrrw(fflags, 0);
    }

    /* 5: the read-only machine ID CSRs, all defined to read zero here. */
    {
        if (csrr(mvendorid) != 0) return 5;
        if (csrr(marchid) != 0) return 5;
        if (csrr(mimpid) != 0) return 5;
        if (csrr(mhartid) != 0) return 5;
        if (csrr(misa) != 0) return 5;
    }

    /* 6: the machine trap CSRs, which are 64-bit views of SPC/EV/EA/SR. */
    {
        csrw(mscratch, 0x0123456789abcdefUL);
        if (csrr(mscratch) != 0x0123456789abcdefUL) return 6;
        csrw(mepc, 0x1000);
        csrw(mtvec, 0x2000);
        csrw(mtval, 0x3000);
        if (csrr(mepc) != 0x1000) return 6;
        if (csrr(mtvec) != 0x2000) return 6;
        if (csrr(mtval) != 0x3000) return 6;
        if (csrr(mscratch) != 0x0123456789abcdefUL) return 6;   /* undisturbed */
    }

    /* 7: x0 survived all of the above.  `csrw' is `csrrw x0, ...', and check 6
     *    used it six times -- so if x0 were writable it now holds a CSR's old
     *    value and this comparison fails. */
    {
        u64 zero;
        __asm__ volatile ("mv %0, zero" : "=r"(zero));
        if (zero != 0) return 7;
        volatile u64 v = 5;
        if ((v & 0) != 0) return 7;
    }

    /* 8: FENCE.I decodes and does not disturb anything. */
    {
        __asm__ volatile ("fence.i" ::: "memory");
        __asm__ volatile ("fence rw, rw" ::: "memory");
        if (csrr(fflags) != 0) return 8;
    }

    rv_write(1, "rv64csr ok\n", 11);
    return 0;
}
