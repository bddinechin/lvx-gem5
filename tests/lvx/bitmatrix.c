typedef unsigned long u64;
/* sbmm8d $d = s1, s2  ->  helper _BMM_8(s2, s1) */
static inline u64 mm(u64 s1, u64 s2){ u64 r; __asm__("sbmm8d %0 = %1, %2\n\t;;" : "=r"(r) : "r"(s1), "r"(s2)); return r; }
/* sbmmt8d $d = s1, s2 ->  _BMT_8(_BMM_8(s2, s1)) */
static inline u64 mt(u64 s1, u64 s2){ u64 r; __asm__("sbmmt8d %0 = %1, %2\n\t;;" : "=r"(r) : "r"(s1), "r"(s2)); return r; }
static inline int fold(u64 x){ return (int)((x ^ (x>>32) ^ (x>>16) ^ (x>>8)) & 0xFF); }
int main(void){
  int acc = 0;
  acc += fold(mm(0x8040201008040201UL, 0x0102040810204080UL));
  acc += fold(mm(0x00000000000000FFUL, 0x0101010101010101UL)); /* memset splat */
  acc += fold(mt(0x8040201008040201UL, 0x0102040810204080UL));
  return acc & 0xFF;
}
