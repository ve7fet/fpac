/*
 * wp/make_tab.c
 *
 * FPAC project
 *
 */

/* The CRC polynomial is represented in binary; each bit is the
coefficient of a term. The low-order bit corresponds to the
highest-order coefficient (yes, this does seem backwards).

The bit sequences for some common CRC polynomials:

CRC-16:	 16    15    2
	x   + x   + x  + 1    0x14003

CCITT:	 16    12    5
	x   + x   + x  + 1    0x10811

The CCITT polynomial is used for Kermit.

*/

#include <stdio.h>

#define CCITT 0x10811L
#define CRC_16 0x14003L

void make_16_bit_crc_tab(unsigned long poly) 
{
	unsigned long i,j, M;
  
  	printf("unsigned short crc_table[256] = {\n");
  	for (i=0; i<256; i++) {
    		M = i;
    		for (j=8;j>0;j--) {
      			if (M & 1) {
      				M ^= poly;
      			}
      			M >>= 1;
      		}
    		printf("%#lx,\n", M);
    	}
  	printf("};\n");
}

int main(int ac, char **av)
{
	make_16_bit_crc_tab(CCITT);
	return 0;
}
