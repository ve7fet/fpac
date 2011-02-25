/*
 *  libmd.c
 *   FPAC project
 *
 */
#include <stdio.h>
#include "global.h"
#include "md5.h"
#include <string.h>

/* 
 * Digests a string and prints the result.
 */
void MD5String (char *dest, char *source)
{
	MD5_CTX context;
	unsigned char digest[16];
	unsigned int len = strlen (source);

	MD5Init (&context);
	MD5Update (&context, (unsigned char *)source, len);
	MD5Final (digest, &context);

	sprintf (dest, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
			 digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7],
			 digest[8], digest[9], digest[10], digest[11], digest[12], digest[13], digest[14], digest[15]);
}
