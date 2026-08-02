/* __int128 compares and arithmetic.

   A 128-bit value lives in a register pair, so this exercises the paired-GPR
   half of the ABI and the multi-word compare/add expansion -- neither of which
   any other program here reaches.  The two operands differ only in the low
   half of the high word, so a compare that looks at the high word alone gets
   the wrong answer.

   Note the ISS has an unimplemented intcomp_128 helper: GCC does not use it
   (it expands the compare into 64-bit pieces), and this test is what says so.  */
int main(void){
    volatile __int128 a = ((__int128)0x1234567890abcdefLL << 40) + 12345;
    volatile __int128 b = ((__int128)0x1234567890abcdeeLL << 40) + 99999;
    int r = 0;
    if (a > b) r += 1;
    if (a < b) r += 2;
    if (a == b) r += 4;
    volatile unsigned __int128 ua = a, ub = b;
    if (ua > ub) r += 8;                      /* same pair, unsigned compare */
    r += (int)((a + b) & 0x3F);               /* carry across the word boundary */
    return r & 0xFF;
}
