void puts_(const char *);
static unsigned char a=1;static unsigned short b=2;
int main(void){unsigned char x=__atomic_fetch_add(&a,3,__ATOMIC_SEQ_CST);unsigned short y=__atomic_fetch_add(&b,4,__ATOMIC_SEQ_CST);puts_(x==1&&a==4&&y==2&&b==6?"ATOMIC OK\n==END==\n":"ATOMIC BAD\n==END==\n");return 0;}
