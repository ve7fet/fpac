/*
* remote.c
* This creates the remote binary.
*
* It is a simple control program for setting and
* clearing bits on a parallel port.
*
* This allows the parallel port pins to be used
* for remote controlling attached devices.
*
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/io.h>

#define PORT 0x3BC

int main (int ac, char **av)
{
	char str[80];
	int port;
	int inval;
	int val;
	int i, v;

	if (ac > 1) 
		sscanf(av[1], "%x", &port);
	else
		port = PORT;
		
	printf ("\nFPAC Parallel port remote control. Address is %x (hex).\n", port);

	if (ioperm (port, 3, 1))
	{
		printf ("open port : %s\n", strerror (errno));
		return 1;
	}

	for (;;)
	{
		inval = inb(port);

		printf ("\nOuputs :                          Inputs:\n");
		for (i = 0; i < 8; i++)
		{
			printf ("B%d  ", i);
		}
		printf ("  ER  SL  PA  AC  BU\n");

		for (i = 0; i < 8; i++)
		{
			printf ("%d   ", (inval >> i) & 1);
		}

		printf ("  ");

		val = inb (port + 1);

		for (i = 3; i < 8; i++)
		{
			if (i == 7)	/* Busy */
				printf ("%d   ", !(val >> i) & 1);
			else
				printf ("%d   ", (val >> i) & 1);
		}
		printf ("\n\nCommand C(bit), S(bit), H, ?, Q\n");

		fgets (str, sizeof(str), stdin);
		
		if ((*str == 'q') || (*str == 'Q'))
			break;

		if ((*str == 'c') || (*str == 'C'))
		{
/*	int	*/	v = str[1] - '0';
			
			if ((v < 0) || (v > 7))
			{
				printf("Error : Command is c0 to c7\n");
				continue;
			}
			v = 1 << v;
			inval &= (~v);
			outb (inval, port);
			usleep (100000);
			inval = inb (port);
		}
		
		else if ((*str == 's') || (*str == 'S'))
		{
/*	int	*/	v = str[1] - '0';
			
			if ((v < 0) || (v > 7))
			{
				printf("Error : Command is s0 to s7\n");
				continue;
			}
			v = 1 << v;
			inval |= v;
			outb (inval, port);
			usleep (100000);
			inval = inb (port);
		}
		
		else if ((*str == 'h') || (*str == 'H') || (*str == '?'))
		{
			printf("Valid command are :\n"
			 	"c0 : Clear output bit 0\n"
			    "s5 : Set   output bit 5\n"
				"h,?: This help\n"
				"q  : Quit\n\n");
		}
		
		else
		{
			printf("Error : Command is C(bit) S(bit) or Q\n");
		}
	}
	
	if (ioperm (port, 3, 0))
	{
		printf ("close port : %s\n", strerror (errno));
		return 1;
	}

	return 0;
}
