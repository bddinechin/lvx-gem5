/* integer division and modulo: signed and unsigned, 32- and 64-bit.

   These are hardware instructions on LVX, not libgcc calls -- lvx-gcc lowers
   them to the divmod family, so the freestanding link stays clean.  That was
   not always true: the backend used to emit an FP-reciprocal sequence whose
   mnemonics the assembler rejected, which is why the corpus avoided '/' and
   '%' entirely.  This is the test that keeps it fixed.

   The operands are volatile so nothing folds at compile time; a constant
   divisor would be turned into a multiply-and-shift and test nothing.  */
int main(void){
    volatile int a=1000, b=7, c=-333, d=11;
    volatile unsigned ua=4000000000u, ub=97u;
    volatile long la=1234567890123L, lb=99991L;
    int r=0;
    r += a/b; r += a%b;                       /* signed 32, positive */
    r += c/d; r += c%d;                       /* signed 32, negative dividend */
    r += (int)(ua/ub); r += (int)(ua%ub);     /* unsigned 32, above INT_MAX */
    r += (int)(la/lb); r += (int)(la%lb);     /* signed 64 */
    return r & 0xFF;
}
