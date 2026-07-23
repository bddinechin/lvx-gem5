/* nested loops, accumulation, early break/continue */
int main(void){
    int s=0;
    for(int i=0;i<12;i++)
        for(int j=0;j<12;j++){
            if(((i^j)&1)==0) continue;
            s += i*j - j;
            if(s>4000) break;
        }
    int k=0; while(s>0){ s-=7; k++; }
    return (k*3) & 0xFF;
}
