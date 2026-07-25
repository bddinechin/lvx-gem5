/* highmult ISS check -- LVX-only, self-checking.
 *
 * The four muld modifier variants exercise the reworked highmult Modifier
 * (.H=0 .HU=1 .HSU=2 .=3) and the generated ISS behavior's SWITCH dispatch:
 *   muld     (.)   -> SWITCH DEFAULT (SXxSX), low  64 half
 *   muld.h   (0)   -> SWITCH DEFAULT (SXxSX), high 64 half
 *   muld.hu  (1)   -> SWITCH CASE.1  (ZXxZX), high 64 half
 *   muld.hsu (2)   -> SWITCH CASE.2  (SXxZX), high 64 half
 *
 * Expected products precomputed (128-bit) for the operands below; a and b both
 * have bit63 set so signed/unsigned extension differ and all four results are
 * distinct. Returns 0 on success, else a bitmask of the failing variant(s):
 *   1=muld  2=muld.h  4=muld.hu  8=muld.hsu   (exit code = return & 255).
 *
 * This can only run on the LVX ISS (muld has no x86 equivalent), so it is a
 * standalone check, not part of the native<->ISS diff harness.
 */
typedef unsigned long u64;

static inline u64 mld    (u64 a, u64 b){ u64 r; __asm__("muld %0 = %1, %2\n;;"     : "=r"(r) : "r"(a), "r"(b)); return r; }
static inline u64 mld_h  (u64 a, u64 b){ u64 r; __asm__("muld.h %0 = %1, %2\n;;"   : "=r"(r) : "r"(a), "r"(b)); return r; }
static inline u64 mld_hu (u64 a, u64 b){ u64 r; __asm__("muld.hu %0 = %1, %2\n;;"  : "=r"(r) : "r"(a), "r"(b)); return r; }
static inline u64 mld_hsu(u64 a, u64 b){ u64 r; __asm__("muld.hsu %0 = %1, %2\n;;" : "=r"(r) : "r"(a), "r"(b)); return r; }

int main(void)
{
  u64 a = 0xFEDCBA9876543210UL, b = 0x89ABCDEF01234567UL;

  /* xor-with-expected keeps the 64-bit constants in make/xord, not in a
   * conditional move (gcc emits cmoved with a 64-bit immediate otherwise,
   * which the assembler rejects -- unrelated to this check). */
  u64 d1 = mld    (a, b) ^ 0x09CA39E1358E7470UL;  /* low  SXxSX */
  u64 d2 = mld_h  (a, b) ^ 0x0086A1C97652E6A9UL;  /* high SXxSX */
  u64 d3 = mld_hu (a, b) ^ 0x890F2A50EDCA5E20UL;  /* high ZXxZX */
  u64 d4 = mld_hsu(a, b) ^ 0xFF635C61ECA718B9UL;  /* high SXxZX */

  int code = 0;
  if (d1) code |= 1;
  if (d2) code |= 2;
  if (d3) code |= 4;
  if (d4) code |= 8;
  return code;
}
