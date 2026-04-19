/*
 *  crc.c
 *
 *  FPAC project
 *
 */
/* The following routines can be used to interpret the crc_table.
  First use init_crc; then add_crc for each character, from
  first to last; and finally, as report_crc to yield the
  result. It is probably much more convenient to code these
  routines inline; this file serves primarily as documentation. */

#include "crc_tab.h"

unsigned short crc16(unsigned char *data, int l_data)
{
	unsigned short crc = 0xFFFF;

	while (l_data-- > 0) {
		crc = crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
        }

	return ~crc;
}

unsigned short crc16_cumul(unsigned char *data, int l_data)
{
	static unsigned short crc;

	if (!data) crc = 0xFFFF;
	while (l_data-- > 0) {
		crc = crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
        }

	return ~crc;
}
