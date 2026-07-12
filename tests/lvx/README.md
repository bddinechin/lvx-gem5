# LVX gem5 SE-mode smoke tests

Minimal freestanding LVX programs that exercise the decode → shim → execute
pipeline end to end and exit via `scall` (kv4-v1 syscall ABI: number in the
scall operand, args in r0..r7, return in r0).

## Build a test (needs the lvx-mbr toolchain on PATH)

```bash
lvx-mbr-as compute.s -o compute.o
lvx-mbr-ld -e _start -Ttext=0x10000 -nostdlib compute.o -o compute.elf
```

## Run under gem5

```bash
build/LVX/gem5.opt tests/lvx/run_lvx.py compute.elf
```

Expected:
- `exit42`  → `target exited (code=42)`
- `compute` → `target exited (code=19)`  (computes 5*3 + 4)
