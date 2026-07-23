/* C switch: dense (jump-table-ish) and sparse arms.  A manual 0..5 cycle avoids
   the constant modulo that -O0 would lower to a libgcc __modsi3 (no libgcc yet). */
int main(void){
    int t=0, m=0;
    for(int i=0;i<40;i++){
        switch(m){
            case 0: t+=1; break;
            case 1: t+=3; break;
            case 2: t+=7; break;
            case 3: t-=2; break;
            case 4: t+=11; break;
            default: t+=i; break;
        }
        if(++m==6) m=0;
        switch(i){ case 10: t*=2; break; case 25: t-=5; break; case 39: t+=100; break; }
    }
    return t & 0xFF;
}
