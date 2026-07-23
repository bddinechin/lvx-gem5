/* integer arithmetic + bitwise mix */
int main(void){
    int a=17,b=5,r=0;
    r += a+b; r += a-b; r += a*b; r += (a<<2); r += (b>>1);
    r ^= 0xAA; r |= 3; r &= 0x1FF; r = r*3 - 11;
    unsigned u = 0xDEADBEEFu; r += (int)(u >> 24); r += (int)(u & 0xF);
    return r & 0xFF;
}
