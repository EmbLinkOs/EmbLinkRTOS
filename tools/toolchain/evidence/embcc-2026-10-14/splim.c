void set(unsigned x){__asm__ volatile("msr msplim, %0"::"r"(x));__asm__ volatile("msr psplim, %0"::"r"(x));}
unsigned get(void){unsigned x;__asm__ volatile("mrs %0, psplim":"=r"(x));return x;}
