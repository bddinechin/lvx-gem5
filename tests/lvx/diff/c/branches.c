/* if/else ladders, ternaries, signed comparisons */
static int cls(int x){
    if(x<0) return 1;
    else if(x==0) return 2;
    else if(x<10) return 3;
    else if(x<100) return 4;
    return 5;
}
int main(void){
    int acc=0;
    for(int x=-20;x<=120;x+=7){
        int c=cls(x);
        acc += (c&1)? c*2 : c+1;
        acc = (x>50)? acc-1 : acc+1;
    }
    return acc & 0xFF;
}
