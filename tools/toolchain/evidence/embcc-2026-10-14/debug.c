volatile int observed;
int increment(int x){int y=x+2;observed=y;return y;}
