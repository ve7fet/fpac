/*
 * libwp.c
 * User library for WP service access
 * FPAC project
 * F1OAT 970831
 */
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "ax25compat.h"
#include "fpac.h"
#include "wp.h"

static int wp_socket = -1;
int wp_debug = 0;

/*****************************************************************************
* Private internal functions section
*****************************************************************************/

static char *strip_zero_ssid(char *call)
{
	char *cp;

	if ((cp = strstr(call, "-0")) != NULL)
		*cp = 0;
	return call;
}

static ax25_address *call_clean(ax25_address *call)
{
	/* Cleans the callsign */
	char *ptr = (char *)call;
	
	ptr[0] &= 0xfe;
	ptr[1] &= 0xfe;
	ptr[2] &= 0xfe;
	ptr[3] &= 0xfe;
	ptr[4] &= 0xfe;
	ptr[5] &= 0xfe;
	ptr[6] &= 0x1e;

	return(call);
}

static int wait_for_local_wp(int timeout)
{
	struct proc_rs *rp, *rlist;
	time_t timer = time(NULL) + timeout;
	
	while (time(NULL) <= timer) {
		if ((rlist = read_proc_rs()) == NULL) {
			if (errno) perror("FPAC: wait_for_local_wp() read_proc_rs");
/*		syslog(LOG_INFO, "FPAC: wait_for_local() time left %lu / %d (-1)", timer - time(NULL), timeout );*/
		continue;
/*		return -1;*/
		}

		for (rp = rlist; rp != NULL; rp = rp->next) {
			if (	(strcasecmp(rp->dest_addr, "*") == 0) && 
				(strcasecmp(rp->dest_call, "*") == 0) &&
				(strcasecmp(rp->src_call, "WP-0") == 0) &&				
				(strcasecmp(rp->dev, "rose0") == 0))
			{
				free_proc_rs(rlist);
/*		syslog(LOG_INFO, "FPAC: wait_for_local() time left %lu / %d (0)", timer - time(NULL), timeout );*/
				return 0;
			}
		}
		free_proc_rs(rlist);
		sleep(1);
/*		syslog(LOG_INFO, "FPAC: wait_for_local() time left %lu / %d", timer - time(NULL), timeout );*/
	}
	syslog(LOG_INFO, "FPAC: wait_for_local() time left %lu / %d (-1)", timer - time(NULL), timeout );
	return -1;
}

/*****************************************************************************
* WP inter-node and internal protocol functions 
*****************************************************************************/

static char pdu_s_cache[ROSE_MTU];
static int  pdu_s = 0;
static int	pdu_s_len = 0;

static char pdu_r_cache[ROSE_MTU];
static int  pdu_r_pos = 0;
static int	pdu_r_len = 0;

void wp_flush_pdu(void)
{
	int rc;
	
	if (pdu_s_len == 0)
		return;

/*	rc = write(pdu_s, pdu_s_cache, pdu_s_len);*/
/*	rc = send(pdu_s, pdu_s_cache, pdu_s_len, MSG_OOB | MSG_DONTWAIT | MSG_NOSIGNAL);*/
	rc = send(pdu_s, pdu_s_cache, pdu_s_len, 0);
	pdu_s_len = 0;

	if (rc <= 0)	{
/*		syslog(LOG_INFO, "wp_flush_pdu() WRITE ERROR - closing wp socket");*/
		close(pdu_s);
	}
}

static int wp_write_pdu(int s, char *buf, int lg)
{
	if ((s != pdu_s) || ((pdu_s_len + lg) > sizeof(pdu_s_cache)))
		wp_flush_pdu();
		
	pdu_s = s;
	memcpy(pdu_s_cache+pdu_s_len, buf, lg);
	pdu_s_len += lg;

	return lg;
}

int ancien(wp_pdu *pdu)
{
	static struct tm *wpdate, *sdate;
	int date_limite = WP_OBSOLETE;
	int jours, annees, anciennete, s_jours, wp_jours, s_annee, wp_annee;
	time_t temps;

	wpdate = localtime(&pdu->data.wp.date);
	wp_annee = wpdate->tm_year;
	wp_jours = wpdate->tm_yday;
	
	time(&temps);
	sdate = localtime(&temps);
	s_annee = sdate->tm_year;
	s_jours = sdate->tm_yday;

	jours = s_jours - wp_jours;
	annees = s_annee - wp_annee;
	
	if(annees == 0)
		anciennete = jours;
	else
		anciennete = (365 - wp_jours) + ((annees-1) * 365) + s_jours;

	if (anciennete > date_limite) {
/*		fprintf(stderr, "WP record REFUSED %d days old - limit is %d\n", anciennete, date_limite);*/
		return (1);
	}
	else {
/*		fprintf(stdout, "ACCEPTED\n");*/
		return (0);
	}
}

int wp_send_pdu(int s, wp_pdu *pdu)
{
	int L = 0, i, rc, len;
	char p[128];
	unsigned char chck;
/*	char str[80];*/

	p[L++] = pdu->type;
	
	if (pdu->type != wp_type_ascii)
		p[L++] = 0; /* Length of data */
	
	switch (pdu->type) {
	case wp_type_set:
	case wp_type_get_response:
		*(unsigned int *)&p[L] = htonl(pdu->data.wp.date);
		L += 4;
		memcpy(&p[L], &pdu->data.wp.address.srose_addr, 5);
		L += 5;
		memcpy(&p[L], &pdu->data.wp.address.srose_call, 7);
		L += 7;
		/* 6 digis maximum */
		if (pdu->data.wp.address.srose_ndigis > 6)
			pdu->data.wp.address.srose_ndigis = 6;
		p[L++] = (unsigned char)pdu->data.wp.address.srose_ndigis;
		for (i=0; i<pdu->data.wp.address.srose_ndigis; i++) {
			memcpy(&p[L], &pdu->data.wp.address.srose_digis[i], 7);
			L += 7;
		}
		
		/* Node information */
		p[L++] = pdu->data.wp.is_node;
		
		/* State information */
/*		p[L++] = pdu->data.wp.is_deleted;*/
		
		/* Do not send deleted records older than WP_OBSOLETE days */
		if ((p[L++] = pdu->data.wp.is_deleted)) {
			if (ancien(pdu))
				return -1;
		}
		
		/* Ascii data */
		len = strlen(pdu->data.wp.name);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.wp.name, len);
			L += len;
		}
		len = strlen(pdu->data.wp.city);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.wp.city, len);
			L += len;
		}
		len = strlen(pdu->data.wp.locator);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.wp.locator, len);
			L += len;
		}
		break;		
	case wp_type_get:
		memcpy(&p[L], &pdu->data.call, 7);
		L += 7;		
		break;
	case wp_type_info:
		*(unsigned int *)&p[L] = htonl(pdu->data.info.mask);
		L += 4;
		break;
	case wp_type_info_response:
		*(unsigned int *)&p[L] = htonl(pdu->data.info_rsp.mask);
		L += 4;
		*(unsigned int *)&p[L] = htonl(pdu->data.info_rsp.nbrec);
		L += 4;
		*(unsigned int *)&p[L] = htonl(pdu->data.info_rsp.size);
		L += 4;
		break;
	case wp_type_get_list:
		*(unsigned int *)&p[L] = htonl(pdu->data.list_req.max);
		L += 4;
		*(unsigned int *)&p[L] = htonl(pdu->data.list_req.flags);
		L += 4;
		memcpy(&p[L], &pdu->data.list_req.mask, 9);
		L += 9;		
		break;
	case wp_type_get_list_response:
		*(unsigned int *)&p[L] = htonl(pdu->data.list_rsp.pos);
		L += 4;
		p[L] = pdu->data.list_rsp.next;
		L += 1;
		*(unsigned int *)&p[L] = htonl(pdu->data.list_rsp.wp.date);
		L += 4;
		memcpy(&p[L], &pdu->data.list_rsp.wp.address.srose_addr, 5);
		L += 5;
		memcpy(&p[L], &pdu->data.list_rsp.wp.address.srose_call, 7);
		L += 7;
		/* 6 digis maximum */
		if (pdu->data.list_rsp.wp.address.srose_ndigis > 6)
			pdu->data.list_rsp.wp.address.srose_ndigis = 6;
		p[L++] = (unsigned char)pdu->data.list_rsp.wp.address.srose_ndigis;
		for (i=0; i<pdu->data.list_rsp.wp.address.srose_ndigis; i++) {
			memcpy(&p[L], &pdu->data.list_rsp.wp.address.srose_digis[i], 7);
			L += 7;
		}
		
		/* Node information */
		p[L++] = pdu->data.list_rsp.wp.is_node;
		
		/* State information */
		p[L++] = pdu->data.list_rsp.wp.is_deleted;
		
		/* Ascii data */
		len = strlen(pdu->data.list_rsp.wp.name);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.list_rsp.wp.name, len);
			L += len;
		}
		len = strlen(pdu->data.list_rsp.wp.city);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.list_rsp.wp.city, len);
			L += len;
		}
		len = strlen(pdu->data.list_rsp.wp.locator);
		p[L++] = len;
		if (len) {
			memcpy(&p[L], pdu->data.list_rsp.wp.locator, len);
			L += len;
		}
		break;
	case wp_type_response:
		p[L++] = pdu->data.status;
		break;
	case wp_type_ascii:
		pdu->data.string[sizeof(pdu->data.string)-1] = 0;
		strncpy(&p[L], pdu->data.string, sizeof(pdu->data.string));
		L += strlen(pdu->data.string);
		break;	
	case wp_type_vector_request:
	case wp_type_vector_response:
		*(unsigned short *)&p[L] = htons(pdu->data.vector.version);
		L += 2;		
		*(unsigned int *)&p[L] = htonl(pdu->data.vector.date_base);
		L += 4;
		*(unsigned short *)&p[L] = htons(pdu->data.vector.interval);
		L += 2;		
		*(unsigned short *)&p[L] = htons(pdu->data.vector.seed);
		L += 2;
		for (i=0; i<WP_VECTOR_SIZE; i++) {
			*(unsigned short *)&p[L] = htons(pdu->data.vector.crc[i]);
			L += 2;		
		}
		break;
	case wp_type_end_transaction:
		break;
	default: 
		syslog(LOG_INFO, "wp_send_pdu() unknown wp type\n");
		return -1;
		break;		
	}

	if (pdu->type != wp_type_ascii)
		p[1] = L-2; /* Length of data without CRC */
			
	/* Compute checksum except for ascii frames */
	if (pdu->type != wp_type_ascii) {
		for (chck=0, i=0 ; i < L ; i++) {
			chck += p[i];
		}
		p[L++] = (~chck + 1);
	}

	rc = wp_write_pdu(s, p, L);
	if (rc < 0) {
	char str[80];
		syslog(LOG_INFO, "[%d] wp_send_pdu write s=%d L=%d\n", getpid(), s, L);
		sprintf(str, "[%d] wp_send_pdu write s=%d L=%d", getpid(), s, L);
		perror(str);
		return rc;
	}
	return 0; /* succes */
}

int wp_receive_pdu(int s, wp_pdu *pdu)
{
	int rc = 0, L = 0, i, len;
	char *p;
	unsigned char chck;
	fd_set rfds;
	struct timeval timeout;
	
	memset(pdu, 0, sizeof(*pdu));
	
	if (pdu_r_pos >= pdu_r_len) {
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		timeout.tv_sec = WP_API_TIMEOUT;
		timeout.tv_usec = 0;
	
		rc = select(s+1, &rfds, NULL, NULL, &timeout);
		if (rc <= 0) {
			syslog(LOG_INFO, "wp_receive_pdu() WP API timeout\n");
/*			fprintf(stderr, "WP API timeout\n");*/
			return rc;
		}
	
		/* Read the packet */
/*		rc = read(s, pdu_r_cache, sizeof(pdu_r_cache));*/
		rc = recv(s, pdu_r_cache, sizeof(pdu_r_cache), 0);
		if (rc <= 0) {
/*			syslog(LOG_INFO, "wp_receive_pdu()rc read disconnection or error - status %d\n", pdu->data.status);*/
			return rc;	/* Disconnection or error */
		}
		pdu_r_pos = 0;
		pdu_r_len = rc;
	}

	p = pdu_r_cache + pdu_r_pos;
	pdu->type = p[0];
	
	switch (pdu->type) {
	case wp_type_set:
	case wp_type_get_response:
	case wp_type_get:
	case wp_type_info:
	case wp_type_info_response:
	case wp_type_get_list:
	case wp_type_get_list_response:
	case wp_type_response:
	case wp_type_vector_request:
	case wp_type_vector_response:
	case wp_type_end_transaction:
		len = p[1];
		pdu_r_pos += (len+2);
		rc = len + 2;
		
		if (pdu_r_pos > pdu_r_len) {
			syslog(LOG_INFO, "Received wp pdu wrong length %d/%d\n", pdu_r_pos, pdu_r_len); 
			return -1; /* Wrong data received */
		}
	
		/* read checksum */
		++pdu_r_pos;
		
		/* Checksum except for ascii commands */
		for (chck=0, i=0 ; i < (rc+1) ; i++) {
			chck += p[i];
		}
	
		if ((rc < 2) || (chck != 0)) {
			syslog(LOG_INFO, "Received wp pdu bad checksum=%02x fm=%d Type=%d\n", chck, s, pdu->type); 
			for (i = 0 ; i < rc ; i++)
				printf("%02x ", p[i] & 0xff);
			printf("\n");
			return -1;
		}
		/* Pointer to data */
		p += 2;
		rc -= 2;
		break;
	default:
		rc -= 1;
		pdu_r_pos = pdu_r_len = 0;
		break;
	}
		
	switch (pdu->type) {
	case wp_type_set:
	case wp_type_get_response:
		if (rc < 21) {	/* Minimum length */
			syslog(LOG_INFO, "Received wp pdu min length/21=%d\n", rc); 
			return -1;
		}
		pdu->data.wp.date = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		memcpy(&pdu->data.wp.address.srose_addr, &p[L], 5);
		L += 5;
		memcpy(&pdu->data.wp.address.srose_call, &p[L], 7);
		L += 7;
		pdu->data.wp.address.srose_ndigis = p[L++];
		/* 5 digis maximum */
		if (pdu->data.wp.address.srose_ndigis > 6)
			pdu->data.wp.address.srose_ndigis = 6;
		for (i=0; i<pdu->data.wp.address.srose_ndigis; i++) {
			memcpy(&pdu->data.wp.address.srose_digis[i], &p[L], 7);
			L += 7;
		}
		
		/* Node information */
		pdu->data.wp.is_node = p[L++];
		
		/* State information */
/*		pdu->data.wp.is_deleted = p[L++];*/
		/* We do not accept deleted records older than WP_OBSOLETE days */
		if ((pdu->data.wp.is_deleted = p[L++])) {
			if (ancien(pdu)) {
/*				syslog(LOG_ERR, "wp_receive_pdu record too old\n"); */
				return -1;
			}
		}
		
		/* Ascii data */
		len = p[L++];
		memset(pdu->data.wp.name, 0, sizeof(pdu->data.wp.name));
		if (len) {
			memcpy(pdu->data.wp.name, &p[L], len);
/* DEBUG F6BVP */
/*			syslog(LOG_ERR, "wp_receive_pdu() get_response name '%s'\n", pdu->data.wp.name);*/
			L += len;
		}
		len = p[L++];
		memset(pdu->data.wp.city, 0, sizeof(pdu->data.wp.city));
		if (len) {
			memcpy(pdu->data.wp.city, &p[L], len);
			L += len;
		}
		len = p[L++];
		memset(pdu->data.wp.locator, 0, sizeof(pdu->data.wp.locator));
		if (len) {
			memcpy(pdu->data.wp.locator, &p[L], len);
			L += len;
		}
		break;		
	case wp_type_get:
		memcpy(&pdu->data.call, &p[L], 7);
		L += 7;		
		break;
	case wp_type_info:
		pdu->data.info.mask = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		break;
	case wp_type_info_response:
		pdu->data.info_rsp.mask = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		pdu->data.info_rsp.nbrec = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		pdu->data.info_rsp.size = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		break;
	case wp_type_get_list:
		pdu->data.list_req.max = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		pdu->data.list_req.flags = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		memcpy(&pdu->data.list_req.mask, &p[L], 9);
		pdu->data.list_req.mask[9] = '\0';
		L += 9;		
		break;
	case wp_type_get_list_response:
		if (rc < 26) {	/* Minimum length */
			syslog(LOG_ERR, "Received wp pdu min length/26=%d\n", rc); 
			return -1;
		}
		pdu->data.list_rsp.pos = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		pdu->data.list_rsp.next = p[L];
		L += 1;
		
		/* Struct wp_t */
		pdu->data.list_rsp.wp.date = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		memcpy(&pdu->data.list_rsp.wp.address.srose_addr, &p[L], 5);
		L += 5;
		memcpy(&pdu->data.list_rsp.wp.address.srose_call, &p[L], 7);
		L += 7;
		pdu->data.list_rsp.wp.address.srose_ndigis = p[L++];
		/* 5 digis maximum */
		if (pdu->data.list_rsp.wp.address.srose_ndigis > 6)
			pdu->data.list_rsp.wp.address.srose_ndigis = 6;
		for (i=0; i<pdu->data.list_rsp.wp.address.srose_ndigis; i++) {
			memcpy(&pdu->data.list_rsp.wp.address.srose_digis[i], &p[L], 7);
			L += 7;
		}
		
		/* Node information */
		pdu->data.list_rsp.wp.is_node = p[L++];
		
		/* State information */
		pdu->data.list_rsp.wp.is_deleted = p[L++];
		
		/* Ascii data */
		len = p[L++];
		memset(pdu->data.list_rsp.wp.name, 0, sizeof(pdu->data.list_rsp.wp.name));
		if (len) {
			memcpy(pdu->data.list_rsp.wp.name, &p[L], len);
			L += len;
		}
		len = p[L++];
		memset(pdu->data.list_rsp.wp.city, 0, sizeof(pdu->data.list_rsp.wp.city));
		if (len) {
			memcpy(pdu->data.list_rsp.wp.city, &p[L], len);
			L += len;
		}
		len = p[L++];
		memset(pdu->data.list_rsp.wp.locator, 0, sizeof(pdu->data.list_rsp.wp.locator));
		if (len) {
			memcpy(pdu->data.list_rsp.wp.locator, &p[L], len);
			L += len;
		}
		break;
	case wp_type_response:
		pdu->data.status = p[L++];
/*		syslog(LOG_ERR, "wp_receive_pdu() wp_type_response status : %d\n", pdu->data.status);*/
		break;
	case wp_type_ascii:
		memcpy(pdu->data.string, &p[L], MIN(rc-L, sizeof(pdu->data.string)));
		pdu->data.string[sizeof(pdu->data.string)-1] = 0;
		L = rc;
		break;
	case wp_type_vector_request:
	case wp_type_vector_response:
		pdu->data.vector.version = ntohs(*(unsigned short *)&p[L]);
		L += 2;		
		pdu->data.vector.date_base = ntohl(*(unsigned int *)&p[L]);
		L += 4;
		pdu->data.vector.interval = ntohs(*(unsigned short *)&p[L]);
		L += 2;		
		pdu->data.vector.seed = ntohs(*(unsigned short *)&p[L]);
		L += 2;
		for (i=0; i<WP_VECTOR_SIZE; i++) {
			pdu->data.vector.crc[i] = ntohs(*(unsigned short *)&p[L]);
			L += 2;		
		}
		break;
	case wp_type_end_transaction:
		break;
	case '\r':
		return -1;
	default: 
		syslog(LOG_INFO, "Received unknown pdu type %d\n", pdu->type);
		return -1;
	}
	
	if (L != rc) {
		syslog(LOG_INFO, "Received wp pdu bad length (type %d, want %d, received %d)\n", pdu->type, L, rc); 
		return -1;
	}
		
	if (pdu_r_pos < pdu_r_len)
		return 2;
		
	pdu_r_pos = pdu_r_len = 0;
	
	return 0;
}

/*****************************************************************************
* Public API section
*****************************************************************************/

/*
 * Check if a callsign is valid or not.
 * 
 * Return 0 if correct or -1 if error
 */
 
int wp_check_call(const char *s)
{
	int len = 0;
	int nums = 0;
	int ssid = 0;
	char *p[1];

	if (s == NULL)
		return -1;
	while (*s && *s != '-') {
		if (!isalnum(*s))
			return -1;
		if (isdigit(*s))
			nums++;
		len++;
		s++;
	}
	if (*s == '-') {
		if (!isdigit(*++s))
			return -1;
		ssid = strtol(s, p, 10);
		if (**p)
			return -1;
	}
	if (len < 4 || len > 6 || nums == 0 || nums > 2 || ssid < 0 || ssid > 15)
		return -1;
	return 0;
}

/*
 * Return the number of valid records in the database
 *
 */
 
int wp_nb_records(void)
{
	wp_pdu pdu;
	int rc;
	
	if (wp_socket == -1)
		return -1;
		
	memset(&pdu, 0, sizeof(wp_pdu));
	pdu.type = wp_type_info;
	pdu.data.info.mask = WP_INFO_ALL;
	
	rc = wp_send_pdu(wp_socket, &pdu);
	if (rc)
	{
		/* Disconnected */
		syslog(LOG_INFO, "wp_nb_records() send wp error - closing wp socket\n"); 
		wp_close();
		return -1;
	}
	
	wp_flush_pdu();
	
	rc = wp_receive_pdu(wp_socket, &pdu);
	if (rc == -1)
	{
		/* Disconnected */
		syslog(LOG_INFO, "wp_nb_records() receive wp error - closing wp socket\n"); 
		wp_close();
		return -1;
	}
	
	if (pdu.type != wp_type_info_response) {
		syslog(LOG_INFO, "wp_nb_records() wrong wp type info response\n"); 
		return -1;
	}
		
	return pdu.data.info_rsp.nbrec;
}

/*
 * Open a connection with the specified server. Connection
 * is open in ROSE mode with localhost.
 * 
 * If remote is NULL, this is a local call !
 *
 * Return the socket handle or -1 if error
 */
 
int wp_open_remote(char *source_call, struct full_sockaddr_rose *remote, int non_block)
{
	struct full_sockaddr_rose rose;
	int fd, rc;
	char *rs_addr = rs_get_addr(NULL);

	if (!rs_addr) return -1;
	
	fd = socket(AF_ROSE, SOCK_SEQPACKET, 0);
/*	syslog(LOG_INFO, "wp_open_remote() fd %d\n", fd); */
	if (fd < 0) return -1;

	memset(&rose, 0x00, sizeof(struct full_sockaddr_rose));
	rose.srose_family = AF_ROSE;
	rose.srose_ndigis = 0;
	ax25_aton_entry(source_call, rose.srose_call.ax25_call);
	rose_aton(rs_addr, rose.srose_addr.rose_addr);
	if (bind(fd, (struct sockaddr *)&rose, sizeof(struct full_sockaddr_rose)) == -1) {
/*		syslog(LOG_INFO, "wp_open_remote() bind\n"); */
		perror("wp_open: bind");
		close(fd);
		return -1;
	}

	
	if (non_block) {
		int flag = 1;
		rc = ioctl(fd, FIONBIO, &flag);
		if (rc) {
/*			syslog(LOG_INFO, "wp_open_remote() ioctl FIONBIO\n"); */
			perror("wp_open_remote:ioctl FIONBIO");
			close(fd);
			return -1;
		}
	}
	
	if (!remote) {	
/*		syslog(LOG_INFO, "wp_open_remote() local WP\n"); */
		/* Wait for local WP server to be ready */
		if (wait_for_local_wp(60)) return -1;
		/* Reuse the rose structure : same X.121 address */
		ax25_aton_entry("WP", rose.srose_call.ax25_call);
		rc = connect(fd, (struct sockaddr *)&rose, sizeof(struct full_sockaddr_rose));		
	}
	else {
		rc = connect(fd, (struct sockaddr *)remote, sizeof(struct full_sockaddr_rose));		
	}
		
	if (rc && (errno != EINPROGRESS)) {
		close(fd);
		return -1;
	}
	return fd;
}

int wp_listen(void)
{
	struct full_sockaddr_rose rose;
	int fd;
	char *rs_addr;
			
	rose.srose_family = AF_ROSE;
	rose.srose_ndigis = 0;
	ax25_aton_entry("WP", rose.srose_call.ax25_call);
	if (!wp_debug) rs_addr = rs_get_addr(NULL);
	else rs_addr = rs_get_addr("rose1");

	if (!rs_addr) return -1;
	rose_aton(rs_addr, rose.srose_addr.rose_addr);
	
	fd = socket(AF_ROSE, SOCK_SEQPACKET, 0);
	if (fd < 0) return -1;
		
	if (bind(fd, (struct sockaddr *)&rose, sizeof(struct full_sockaddr_rose)) == -1) {
		perror("wp_listen: bind");
		close(fd);
		return -1;
	}

	listen(fd, 5);
	return fd;
}

/*
 * Open a connection with the specified server. Connection
 * is open in ROSE mode with localhost.
 *
 * Return 0 if sucessful
 */
 
int wp_open(char *client)
{	
	wp_socket = wp_open_remote(client, NULL, 0);
	if (wp_socket < 0) return -1;
	return 0;
}

/*
 * Tells a client if WP is opened
 *
 * Return 1 if opened, 0 if closed
 */
 
int wp_is_open(void)
{	
	return (wp_socket != -1);
}

/*
 * Close the currently selected WP connection
 *
 */
 
void wp_close()
{
	if (wp_socket >=0) {
		close(wp_socket);
		wp_socket = -1;
	}
}


/*
 * Update (or create) a WP record
 *
 * Return 0 if successful 
 */
 
int wp_update_addr(struct full_sockaddr_rose *addr)
{
	wp_t wp;
	char *ptr;

	/* Only valid callsigns ! */
	ptr = ax25_ntoa(&addr->srose_call);
	if ((ptr == NULL) || (wp_check_call(ptr) != 0)) {
		syslog(LOG_INFO, "wp_update_addr() invalid callsign '%s'\n", ptr);
		return -1;
	}
	memset(&wp, 0, sizeof(wp_t));		
	if (wp_get(&addr->srose_call, &wp) != 0) {
		syslog(LOG_INFO, "wp_update_addr() callsign '%s' not found\n", ptr);
		return -1;
	}
	wp.date = time(NULL);
	wp.is_deleted = 0;
	wp.address = *addr;
	return wp_set(&wp);
}

/*
 * Search a WP record and return associated full_sockaddr_rose.
 *
 * Return 0 if found.
 */
 
int wp_search(ax25_address *call, struct full_sockaddr_rose *addr)
{
	wp_t wp;
	int rc;

	rc = wp_get(call, &wp);
	if (rc ) {
/*DEBUG F6BVP */
		syslog(LOG_INFO, "wp_search() callsign '%s'\n", ax25_ntoa(call));
		return -1;
	}

	if (wp.is_deleted) return -1;
	
	*addr = wp.address;
	return 0;
}

/*
 * Search and return a list of WP records.
 * Return 0 if ok.
 * Return -1 if not found / error.
 */
 
int wp_get_list(wp_t **wp, int *nb, int flags, char *mask)
{
	wp_pdu pdu;
	wp_t *wptab = NULL;
	int rc;
	int max = *nb;
	int i = 0;;
	
	if (wp_socket == -1) {
		syslog(LOG_INFO, "wp_get_list() no wp socket\n");
		return -1;
	}

	*wp = NULL;
	*nb = 0;
	
	memset(&pdu, 0, sizeof(wp_pdu));
	pdu.type = wp_type_get_list;
	pdu.data.list_req.mask[9] = '\0';
	pdu.data.list_req.flags = flags;
	pdu.data.list_req.max = max;
	
	memcpy(pdu.data.list_req.mask, mask, 9);
	rc = wp_send_pdu(wp_socket, &pdu);
	if (rc)
	{
		/* Disconnected */
		syslog(LOG_INFO, "wp_get_list() error disconnected - closing wp socket\n");
		wp_close();
		return -1;
	}
	
	wp_flush_pdu();
	
	for (i = 0 ; i < max ; i++) {
		rc = wp_receive_pdu(wp_socket, &pdu);
		if (rc == -1)
		{
			syslog(LOG_INFO, "wp_get_list() error - closing wp socket\n");
			wp_close();
			return -1;
		}
		if (pdu.type == wp_type_get_list_response) {
			if (wptab == NULL) {
				wptab = calloc(max, sizeof(wp_t));
				*wp = wptab;
			}
			wptab[i] = pdu.data.list_rsp.wp;
			wptab[i].address.srose_family = AF_ROSE;
		}
		else if ((pdu.type == wp_type_response) && (pdu.data.status == WP_OK))
			return 0;
		else {
			syslog(LOG_INFO, "wp_get_list() error pdu type %d wp_type_response %d status %d\n", pdu.type, wp_type_response, pdu.data.status);
			return -1;
		}
		if (pdu.data.list_rsp.next == 0)
		{
			*nb = i+1;
			return 0;
		}
	}
	
	/* End of list */
	return -1;
}

void wp_free_list(wp_t **wp)
{
	if (*wp)
		free (*wp);
	*wp = NULL;
}

/*
 * Search and return a WP record.
 *
 * Return 0 if found.
 */
 
int wp_get(ax25_address *call, wp_t *wp)
{
	wp_pdu pdu;
	int rc;
	char *callsign;
	
	if (wp_socket == -1)
		return -1;
		
	call_clean(call);
	memset(&pdu, 0, sizeof(wp_pdu));
	pdu.type = wp_type_get;
	memcpy(&callsign, &call, 6); 	
	strip_zero_ssid(callsign);
	memcpy(&call, &callsign, 6); 	
	pdu.data.call = *call;
	rc = wp_send_pdu(wp_socket, &pdu);
	if (rc)
	{
		/* Disconnected */
		syslog(LOG_INFO, "wp_get() error disconnected - closing wp socket\n");
		wp_close();
		return -1;
	}

	wp_flush_pdu();
	
	rc = wp_receive_pdu(wp_socket, &pdu);
	if (rc == -1)
	{
		syslog(LOG_INFO, "wp_get() error - wp_receive_pdu() - closing wp socket\n");
		wp_close();
		return -1;
	}
	if (pdu.type == wp_type_get_response) {
		*wp = pdu.data.wp;
		wp->address.srose_family = AF_ROSE;
		return 0;
	}
	else {
		syslog(LOG_INFO, "wp_get() error pdu.type %d - closing wp socket\n", pdu.type);
	}
	return -1;
}

/*
 * Update (or create) a full WP record
 *
 * Return 0 if successful
 */
 
int wp_set(wp_t *wp)
{
	int n;
	int rc;
	wp_pdu pdu;
	
	if (wp_socket == -1) {
		syslog(LOG_ERR, "wp_set() no wp socket\n");
		return -1;
	}	
	wp->date = time(NULL);
	call_clean(&wp->address.srose_call);
	for (n = 0 ; n < wp->address.srose_ndigis ; n++)
		call_clean(&wp->address.srose_digis[n]);
		
	memset(&pdu, 0, sizeof(wp_pdu));
	pdu.type = wp_type_set;
	pdu.data.wp = *wp;		
	rc = wp_send_pdu(wp_socket, &pdu);
	if (rc)
	{
		/* Disconnected */
		syslog(LOG_INFO, "wp_set() wp_send_pdu() error - closing wp socket\n");
		wp_close();
		return -1;
	}
	
	wp_flush_pdu();
	
	rc = wp_receive_pdu(wp_socket, &pdu);
	if (rc == -1)
	{
		syslog(LOG_INFO, "wp_set() wp_receive_pdu() error - closing wp socket\n");
		wp_close();
		return -1;
	}
	if ((pdu.type == wp_type_response) && (pdu.data.status == WP_OK)) {
/* F6BVP */	
		syslog(LOG_INFO, "wp_set() creates or updates a full WP record\n");
		return 0;
	}
				
	syslog(LOG_ERR, "wp_set() error - type %d response %d name '%s' status %d\n", pdu.type, wp_type_response, pdu.data.wp.name, pdu.data.status);
	return -1;
}

/*
 * Check if a callsign is known node
 *
 * Return 1 of found, 0 if not found/node 
 */
int wp_is_node(char *callsign)
{
	wp_t wp;
	ax25_address call;
	
	ax25_aton_entry(callsign, call.ax25_call);
	
	return ((wp_get(&call, &wp) == 0) && (wp.is_node));
}

#define  AX25_HBIT 0x80
void dump_ax25(char *title, struct full_sockaddr_ax25 *ax25)
{
	int i;

	printf("%s\n", title);
	printf("\tFamily   = %d\n", ax25->fsa_ax25.sax25_family);
	printf("\tCallsign = %s\n", ax25_ntoa(&ax25->fsa_ax25.sax25_call));
	printf("\tndigis   = %d\n", ax25->fsa_ax25.sax25_ndigis);

	for (i = 0; i < ax25->fsa_ax25.sax25_ndigis ; i++)
	{
		if (i == 6)
		{
			printf("Error : Nb digis = %d\n", ax25->fsa_ax25.sax25_ndigis);
			break;
		}
		printf("\tdigi %d   = %s%c\n",
			i+1, 
			ax25_ntoa(&ax25->fsa_digipeater[i]),
			(ax25->fsa_digipeater[i].ax25_call[6] & AX25_HBIT) ? '*' : ' ');
	}
	printf("\n");
}

void dump_rose(char *title, struct full_sockaddr_rose *rose)
{
	int i;

	printf("%s\n", title);
	printf("\tFamily   = %d\n", rose->srose_family);
	printf("\tCallsign = %s\n", ax25_ntoa(&rose->srose_call));
	printf("\tAddress  = %s\n", rose_ntoa(&rose->srose_addr));
	printf("\tndigis   = %d\n", rose->srose_ndigis);

	for (i = 0; i < rose->srose_ndigis ; i++)
	{
		if (i == 6)
		{
			printf("Error : Nb digis = %d\n", rose->srose_ndigis);
			break;
		}
		printf("\tdigi %d   = %s\n", i+1, ax25_ntoa(&rose->srose_digis[i]));
	}
	printf("\n");
}

int strmatch (char *chaine, char *masque)
{
	while (1)
	{
		switch (*masque)
		{
		case '\0':
			return (toupper(*masque) == toupper(*chaine));
		case '&':
			if ((*chaine == '\0') || (*chaine == '.'))
				return (1);
			break;
		case '?':
			if (!isalnum (*chaine))
				return (0);
			break;
		case '#':
			if ((*chaine != '#') && (!isdigit (*chaine)))
				return (0);
			break;
		case '@':
			if (!isalpha (*chaine))
				return (0);
			break;
		case '=':
			if (!isgraph (*chaine))
				return (0);
			break;
		case '*':
			while (*++masque == '*')
				;
			if (*masque == '\0')
				return (1);
			while (!strmatch (chaine, masque))
				if (*++chaine == '\0')
					return (0);
			break;
		default:
			if ((toupper (*chaine)) != (toupper (*masque)))
				return (0);
			break;
		}
		++chaine;
		++masque;
	}
}

