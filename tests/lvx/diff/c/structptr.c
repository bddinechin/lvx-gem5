/* structs, pointers, aggregate copy (exercises the memcpy lowering path) */
struct P { int x, y, tag; };
static struct P mk(int x,int y){ struct P p; p.x=x; p.y=y; p.tag=x^y; return p; }
static int dot(const struct P *a, const struct P *b){ return a->x*b->x + a->y*b->y + (a->tag&b->tag); }
int main(void){
    struct P arr[8];
    for(int i=0;i<8;i++) arr[i]=mk(i+1, (i*3)&7);
    struct P acc = arr[0];
    int s=0;
    for(int i=1;i<8;i++){ s += dot(&acc, &arr[i]); acc = arr[i]; }
    return s & 0xFF;
}
