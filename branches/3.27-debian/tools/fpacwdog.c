/*
 * fpacwdog.c
 *
 * FPAC project
 *
 * fpacwdog : sends a CR to a serial port
 * Default period is 1 minute
 *
 * F6FBB 09-99
 *
 */
 
#include <stdio.h>
#include <termios.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int p;
	int fd;
	char *device;
	struct termios tio;
	int period = 60;	/* default 1 minute */
	int baud = 50;		/* default 50 bauds */
	int is_daemon = 1;	/* default daemon mode */
	int verbose = 0;	/* default not verbose */
	int pid;

	while ((p = getopt (argc, argv, "dvp:b:")) != -1)
	{
		switch (p)
		{
		case 'p':
			period = atoi(optarg);
			break;
		case 'b':
			baud = atoi(optarg);
			break;
		case 'd':
			is_daemon = 0;
			break;
		case 'v':
			++verbose;
			break;
		case '?':
		case ':':
			fprintf (stderr, "usage: fpacwdog [-d] [-b baudrate] [-p seconds] serial_device\n");
			return 1;
		}
	}

	if (optind == argc)
	{
		fprintf (stderr, "usage: fpacwdog [-d] [-b baudrate] [-p seconds] serial_device\n");
		return 1;
	}

	device = argv[optind];

	if (verbose)
	{
		printf("Period : %d second(s)\n", period);
		printf("Baudrate %d\n", baud);
	}
			
	switch (baud)
	{
	case 50:
		baud = B50;
		break;
	case 75:
		baud = B75;
		break;
	case 110:
		baud = B110;
		break;
	case 150:
		baud = B150;
		break;
	case 200:
		baud = B200;
		break;
	case 300:
		baud = B300;
		break;
	case 600:
		baud = B600;
		break;
	case 1200:
		baud = B1200;
		break;
	case 2400:
		baud = B2400;
		break;
	case 4800:
		baud = B4800;
		break;
	case 9600:
		baud = B9600;
		break;
	case 19200:
		baud = B19200;
		break;
	case 38400:
		baud = B38400;
		break;
	default:
		fprintf (stderr, "Invalid baudrate %d\n", baud);
		return 1;
	}
	
	fd = open(device, O_RDWR);
	if (fd == -1)
	{
		fprintf (stderr, "Cannot open serial device %s\n", device);
		return 1;
	}
	
	if (tcgetattr(fd, &tio) == 0)
	{
		cfsetospeed(&tio, baud);
		tcsetattr(fd, TCSANOW, &tio);
	}
	
	if (is_daemon)
	{
/* int	*/	pid = fork();
		if (pid == -1)
		{
			fprintf(stderr, "fpacwdog cannot become daemon\n");
			exit(1);
		}
		if (pid > 0)
		{
			if (verbose)
				printf("Background mode [%d]\n", pid);
			/* Father ends */
			return 0;
		}
	}
	else
	{
		if (verbose)
			printf("Foreground mode\n");
	}
		
	for (;;)
	{
		if (verbose > 1)
			printf("Sending CR\n");
		write(fd, "\r", 1);
		sleep(period);
	}
}
