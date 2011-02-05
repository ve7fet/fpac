
/******************************************************
 *  procutils.c                                       *
 * FPAC project.            Utilities                 *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>

#include "ax25compat.h"
#include "fpac.h"
#include "wp.h"

/*
 * Version of strncpy() that returns NULL if either src or dest is NULL
 * and also makes sure destination string is always terminated.
 */
static char *safe_strncpy(char *dest, char *src, int n)
{
	if (dest == NULL || src == NULL)
		return NULL;
	dest[n] = 0;
	return strncpy(dest, src, n);
}

/*
 * Version of atoi() that returns zero if s == NULL.
 */
static int safe_atoi(const char *s)
{
	return (s == NULL) ? 0 : atoi(s);
}

/*
 * Version of nr_config_load_ports() that do not report errors.
 */
void fpac_nr_config_load_ports(void)
{
	int fd = open("/dev/null", O_WRONLY);
	if (dup2(2, fd) != -1)
	{
		nr_config_load_ports();
		close(fd);
	}
}

/*
 *	fpac -> ascii conversion
 */
char *fpac2asc(rose_address * a)
{
	static char buf[12];

	sprintf(buf, "%02X%02X,%02X%02X%02X",
			a->rose_addr[0] & 0xFF,
			a->rose_addr[1] & 0xFF,
			a->rose_addr[2] & 0xFF,
			a->rose_addr[3] & 0xFF, a->rose_addr[4] & 0xFF);

	return buf;
}

char *rs_get_addr(char *dev)
{
	struct proc_rs *rp, *rlist;
	char *cp = (dev) ? dev : "rose0";
	static char add[11] = "          ";

	if ((rlist = read_proc_rs()) == NULL)
	{
		if (errno)
			perror("do_links: read_proc_rose");
		return NULL;
	}

	for (rp = rlist; rp != NULL; rp = rp->next)
	{
		if ((strcasecmp(rp->dest_addr, "*") == 0) &&
			(strcasecmp(rp->dest_call, "*") == 0) &&
			(strcasecmp(rp->dev, cp) == 0))
		{
			memcpy(add, rp->src_addr, 10);
			break;
		}
	}
	add[10] = '\0';
	free_proc_rs(rlist);
	return (add);
}

/* is_heard : from awznode */
int is_heard(char **av)
{
	FILE *fp;
	static char port[14];
	static char dest[10];
	static char digi[10][AX25_MAX_DIGIS];
	struct mheard_struct mh;
	char call[12];
	char *cp;
	int k;

	if ((fp = fopen(DATA_MHEARD_FILE, "r")) == NULL) {
		printf("Error : file %s not found\n", DATA_MHEARD_FILE);
		return 0;
	}

	safe_strncpy(call, av[0], 9);
	cp = strchr(call, '-');
	if (cp == NULL)
		strcat(call, "-0");

	while (fread(&mh, sizeof(struct mheard_struct), 1, fp) == 1)
	{
		if (strcasecmp(call, ax25_ntoa(&mh.from_call)) == 0)
		{
			fclose(fp);

			safe_strncpy(port, mh.portname, 13);
			safe_strncpy(dest, ax25_ntoa(&mh.from_call), 9);

			av[0] = port;
			av[1] = dest;

			for (k = 0; k < AX25_MAX_DIGIS; k++)
			{
				if (k <= mh.ndigis)
				{
					safe_strncpy(digi[k], ax25_ntoa(&mh.digis[k]), 9);
					av[2 + k] = digi[k];
				}
				else
				{
					av[2 + k] = NULL;
					break;
				}
			}
			return 1;
		}
	}

	fclose(fp);

	return 0;
}

/*
 * Flexnet routines taken from FlexNode
 */

struct flex_gt *read_flex_gt(void) {
    FILE *fp;
    char buffer[256], *cp;
    char *addr;
    struct flex_gt *p = NULL, *list = NULL, *new_el;
    int i = 0, k;
    errno = 0;
    if ((fp = fopen(FLEX_GT_FILE, "r")) == NULL) {
        printf("Error : file %s not found\n", FLEX_GT_FILE);
        return NULL;
    }

    while (fgets(buffer, 256, fp) != NULL) {
        if (i++ < 1)
            continue;
        if (buffer == NULL)
            continue;
        if ((new_el = calloc(1, sizeof (struct flex_gt))) == NULL)
            break;

        new_el->addr = safe_atoi(strtok(buffer, " \t\n\r"));
        safe_strncpy(new_el->call, strtok(NULL, " \t\n\r"), 9);
        safe_strncpy(new_el->dev, strtok(NULL, " \t\n\r"), 13);

        k = 0;
        while ((cp = strtok(NULL, " \t\n\r")) != NULL && k < AX25_MAX_DIGIS)
            safe_strncpy(new_el->digis[k++], cp, 9);
        while (k < AX25_MAX_DIGIS)
            strcpy(new_el->digis[k++], "\0");

        if ((addr = ax25_config_get_name(new_el->dev)) == NULL) {
            /*			nr_config_load_ports();*/

            if ((addr = nr_config_get_name(new_el->dev)) == NULL) {
                /*			rs_config_load_ports();*/
                if ((addr = rs_config_get_name(new_el->dev)) == NULL) {
                    fprintf(stderr,
                            "read_flex_gt: invalid port setting\n");
                    return NULL;
                } else {
                    new_el->af_mode = AF_ROSE;
                }
            } else {
                new_el->af_mode = AF_NETROM;
            }
        } else {
            new_el->af_mode = AF_FLEXNET;
        }

        if (list == NULL) {
            list = new_el;
            p = list;
        } else {
            p->next = new_el;
            p = p->next;
        }
    }
    fclose(fp);
    return list;
}

void free_flex_gt(struct flex_gt *fp)
{
	struct flex_gt *p;

	while (fp != NULL)
	{
		p = fp->next;
		free(fp);
		fp = p;
	}
}

struct flex_dst *read_flex_dst(void)
{
	FILE *fp;
	char buffer[256];
	struct flex_dst *p = NULL, *list = NULL, *new_el;
	int i = 0;

	errno = 0;
  	if ((fp = fopen(FLEX_DST_FILE, "r")) == NULL) {
		printf("Error : file %s not found\n", FLEX_DST_FILE);
	return NULL;
  	}

	while (fgets(buffer, 256, fp) != NULL)
	{
		if (i++ < 1)
			continue;
		if (buffer == NULL)
			continue;
		if ((new_el = calloc(1, sizeof(struct flex_dst))) == NULL)
			break;

		safe_strncpy(new_el->dest_call, strtok(buffer, " \t\n\r"), 9);
		new_el->ssida = safe_atoi(strtok(NULL, " -\t\n\r"));
		new_el->sside = safe_atoi(strtok(NULL, " -\t\n\r"));
		new_el->rtt = safe_atoi(strtok(NULL, " \t\n\r"));
		new_el->addr = safe_atoi(strtok(NULL, " \t\n\r"));

		if (list == NULL)
		{
			list = new_el;
			p = list;
		}
		else
		{
			p->next = new_el;
			p = p->next;
		}
	}
	fclose(fp);
	return list;
}

void free_flex_dst(struct flex_dst *fp)
{
	struct flex_dst *p;

	while (fp != NULL)
	{
		p = fp->next;
		free(fp);
		fp = p;
	}
}

struct flex_dst *find_dest(char *dest_call, struct flex_dst *list)
{
	static struct flex_dst f;
	struct flex_dst *fdst = NULL, *p;
	char *cp, call[10];
	int ssid;

	safe_strncpy(call, dest_call, 9);
	cp = strchr(call, '-');
	if (cp == NULL)
		ssid = 0;
	else
	{
		ssid = safe_atoi(cp + 1);
		*cp = '\0';
	}

	fdst = list ? list : read_flex_dst();
	for (p = fdst; p != NULL; p = p->next)
	{
		if (!strcasecmp(call, p->dest_call)
			&& (ssid >= p->ssida && ssid <= p->sside))
		{
			f = *p;
			f.next = NULL;
			p = &f;
			break;
		}
	}
	if (list == NULL)
		free_flex_dst(fdst);
	return p;
}

struct flex_gt *find_gateway(int addr, struct flex_gt *list)
{
	static struct flex_gt f;
	struct flex_gt *flgt = NULL, *p;

	flgt = list ? list : read_flex_gt();
	for (p = flgt; p != NULL; p = p->next)
	{
		if (addr == p->addr)
		{
			f = *p;
			f.next = NULL;
			p = &f;
			break;
		}
	}
	if (list == NULL)
		free_flex_gt(flgt);
	return p;
}
