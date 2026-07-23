/* arrays: fill, reduce, index-dependent writes */
int main(void){
    int a[32];
    for(int i=0;i<32;i++) a[i] = (i*i + 3*i + 1);
    for(int i=1;i<32;i++) a[i] += a[i-1];
    int s=0;
    for(int i=0;i<32;i+=3) s ^= a[i];
    return (s + a[31]) & 0xFF;
}
