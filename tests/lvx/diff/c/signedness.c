/* signed/unsigned compares, casts, sign extension, negatives */
int main(void){
    int r=0;
    for(int i=-8;i<8;i++){
        unsigned u=(unsigned)i;
        if(u > 100u) r+=2; else r-=1;
        signed char sc=(signed char)(i*40);
        unsigned char uc=(unsigned char)(i*40);
        r += (int)sc; r += (int)uc;
        long L=(long)i * -3; r += (int)(L & 0x3F);
    }
    return r & 0xFF;
}
