/* many-argument calls (register + stack-passed args), forwarding */
static int g8(int a,int b,int c,int d,int e,int f,int g,int h){
    return a - b + c*2 - d + e - f + g*3 - h;
}
static int g10(int a,int b,int c,int d,int e,int f,int g,int h,int i,int j){
    return g8(a,b,c,d,e,f,g,h) + i*4 - j;
}
int main(void){
    int s=0;
    for(int k=0;k<8;k++)
        s += g10(k,k+1,k+2,k+3,k+4,k+5,k+6,k+7,k+8,k+9);
    return s & 0xFF;
}
