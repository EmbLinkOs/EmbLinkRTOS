#include <string.h>
#include "tools/embdbg/remote.h"
int main(void){const struct rsp_regdef *r=rsp_regs_for("avr");if(!r)return 1;for(int i=0;i<32;i++)if(r[i].off!=i||r[i].size!=1)return 2;return strcmp(r[32].name,"sreg")||r[32].off!=32||r[32].size!=1||strcmp(r[33].name,"sp")||r[33].off!=33||r[33].size!=2||strcmp(r[34].name,"pc")||r[34].off!=35||r[34].size!=4||r[35].name!=0;}
