#!/usr/bin/env python3
"""Emit a hand-encoded RV64 ELF for the LVX ISS's RV64G personality.

WHY THIS EXISTS: ADR-0004 says LVX ships no RISC-V assembler -- the upstream
riscv64-unknown-elf toolchain builds RISC-V code -- and none is installed on
this machine yet.  So the first RV-mode tests are encoded by hand here.  The
day that toolchain exists, write the tests as .s files and delete this.

The ELF is tagged EM_RISCV, which is what makes the ISS enter RV mode: gem5's
elf_object.cc claims an EM_RISCV/ELFCLASS64 image as loader::LvxRv64, and
process.cc sets PCState.rv() from it (see docs/riscv-mode.md).

Usage: python3 mkrv.py [outdir]     (default: this directory)
"""
import os
import struct
import sys

BASE = 0x10000          # the load address the native tests use too

# --- RV32I/RV64I encodings (the three seeded ALU forms, plus the two IEnv) ----

X = {f"x{i}": i for i in range(32)}
X.update(zero=0, ra=1, sp=2, t0=5, t1=6,
         a0=10, a1=11, a2=12, a3=13, a4=14, a5=15, a6=16, a7=17)


def lui(rd, imm20):
    return ((imm20 & 0xFFFFF) << 12) | (X[rd] << 7) | 0x37


def addi(rd, rs1, imm12):
    return ((imm12 & 0xFFF) << 20) | (X[rs1] << 15) | (0 << 12) \
        | (X[rd] << 7) | 0x13


def add(rd, rs1, rs2):
    return (0 << 25) | (X[rs2] << 20) | (X[rs1] << 15) | (0 << 12) \
        | (X[rd] << 7) | 0x33


def ecall():
    return 0x00000073


def ebreak():
    return 0x00100073


def li_addr(rd, addr):
    """The %hi/%lo pair for a 32-bit absolute address: lui + addi."""
    hi = (addr + 0x800) >> 12
    lo = addr - (hi << 12)          # in [-2048, 2047]
    return [lui(rd, hi), addi(rd, rd, lo)]


# --- the program ------------------------------------------------------------
#
#   write(1, msg, len(msg))      -- proves the arg ABI (a0..a2 from x10..x12)
#                                   and that the shim reaches target memory
#   exit(written + 26)           -- the count write RETURNED, so the result
#                                   half of the ABI (a0) is proved too, not
#                                   just the argument half; the add makes the
#                                   exit code 42, which no decode that
#                                   silently did nothing could produce
#
# The message address is known because the load address is fixed, so no
# relocation is needed: a lui/addi pair of the final address.

MSG = b"hello from rv64\n"


def build():
    # The code is emitted first, then the message, so the message's address
    # depends on the code size -- which is fixed, so compute it in two passes.
    def code(msg_addr):
        insns = []
        insns.append(addi("a0", "zero", 1))             # fd = 1 (stdout)
        insns += li_addr("a1", msg_addr)                # buf
        insns.append(addi("a2", "zero", len(MSG)))      # count
        insns.append(addi("a7", "zero", 64))            # SYS_write
        insns.append(ecall())                           # a0 = bytes written
        insns.append(addi("t0", "zero", 42 - len(MSG)))  # 26
        insns.append(add("a0", "a0", "t0"))             # 16 + 26 -> 42
        insns.append(addi("a7", "zero", 93))            # SYS_exit
        insns.append(ecall())
        return insns

    n = len(code(0))                        # pass 1: how many instructions
    msg_addr = BASE + 4 * n
    text = b"".join(struct.pack("<I", w) for w in code(msg_addr))
    assert len(text) == 4 * n               # the address did not shift
    return text + MSG


def elf64(payload, entry, base):
    """A minimal ELF64/little-endian/EM_RISCV executable: one PT_LOAD."""
    EHDR, PHDR = 64, 56
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    ehdr = e_ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,            # e_type  = ET_EXEC
        243,          # e_machine = EM_RISCV
        1,            # e_version
        entry,        # e_entry
        EHDR,         # e_phoff
        0,            # e_shoff
        0,            # e_flags  (no float ABI claim: the ISS does not read it)
        EHDR,         # e_ehsize
        PHDR, 1,      # e_phentsize, e_phnum
        0, 0, 0)      # e_shentsize, e_shnum, e_shstrndx -- no section table.
                      # e_shentsize must be 0 too, not sizeof(Shdr): with a
                      # non-zero size and e_shnum 0, readelf takes the real
                      # count from section 0 and reads past the end of the
                      # file.  gem5 loads it either way (it reads the program
                      # headers), but the spurious readelf error on a test
                      # artifact is worth not leaving behind.
    off = EHDR + PHDR
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,                    # p_type = PT_LOAD
        5,                    # p_flags = R|X
        off,                  # p_offset
        base, base,           # p_vaddr, p_paddr
        len(payload),         # p_filesz
        len(payload),         # p_memsz
        0x1000)               # p_align
    assert len(ehdr) == EHDR and len(phdr) == PHDR
    return ehdr + phdr + payload


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.abspath(__file__))
    payload = build()
    path = os.path.join(outdir, "rv_hello.elf")
    with open(path, "wb") as f:
        f.write(elf64(payload, BASE, BASE))
    os.chmod(path, 0o755)
    print(f"{path}: {len(payload)} bytes of text+data at 0x{BASE:x}, "
          f"entry 0x{BASE:x}")
    print(f"  expects: {MSG!r} on stdout, then exit code 42")


if __name__ == "__main__":
    main()
