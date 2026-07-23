/* bit manipulation: popcount, bit-reverse, leading-zero by loop */
static int popc(unsigned x){ int c=0; while(x){ c+=x&1; x>>=1; } return c; }
static unsigned rev8(unsigned x){ unsigned r=0; for(int i=0;i<8;i++){ r=(r<<1)|(x&1); x>>=1; } return r; }
static int nlz32(unsigned x){ int n=0; while(n<32 && !(x&0x80000000u)){ n++; x<<=1; } return n; }
int main(void){
    int s=0;
    for(unsigned v=1;v<200;v+=13){ s+=popc(v); s+=(int)rev8(v); s+=nlz32(v); }
    return s & 0xFF;
}
