/* recursion: fib + factorial (call/ret, RA save/restore) */
static int fib(int n){ return n<2? n : fib(n-1)+fib(n-2); }
static int fact(int n){ return n<=1? 1 : n*fact(n-1); }
int main(void){ return (fib(15) + fact(6)) & 0xFF; }
