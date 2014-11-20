#ifndef __WP_H
#define __WP_H

/*#include <netinet/in.h>*/
/*
 * #include <linux/ax25.h>
 * #include <linux/rose.h>
 */
#include <sys/socket.h>

#define WP_VECTOR_SIZE	16
#define WP_API_TIMEOUT	30	/* Timeout for access to wp server */
#define WP_OBSOLETE 14		/* Days after deleted record is not accepted */

#define WP_NODE_FLAG		1
#define WP_REVERSE_FLAG		2
#define WP_ADDRSORT_FLAG	4
#define WP_DATESORT_FLAG	8

#define WP_INFO_ALL			0xffffffff

#define MIN(a,b)	((a) < (b) ? (a) : (b))

typedef struct {
	time_t date;
	struct full_sockaddr_rose address;
	char name[22];
	char city[22];
	char locator[7];
	char is_node;
	char is_deleted;
	char free[23];		/* For futur extension */
} wp_t;

typedef struct {
	time_t		date_base;
	unsigned short	version;
	unsigned short	interval;
	unsigned short	seed;
	unsigned short	crc[WP_VECTOR_SIZE];	
	int	cnt[WP_VECTOR_SIZE];	
} vector_t;

typedef struct {
	unsigned int max;
	unsigned int flags;
	char mask[10];	
} list_req_t;

typedef struct {
	int pos;
	char next;
	wp_t wp;
} list_rsp_t;

typedef struct {
	int mask;
} info_t;

typedef struct {
	unsigned int mask;
	unsigned int nbrec;
	unsigned int size;
} info_rsp_t;

typedef enum {
	wp_type_set = 0,
	wp_type_get,
	wp_type_get_response,
	wp_type_response,
	wp_type_vector_request,
	wp_type_vector_response,
	wp_type_get_list,
	wp_type_get_list_response,
	wp_type_info,
	wp_type_info_response,
	wp_type_end_transaction,
	wp_type_ascii = ':',
} wp_pdu_type;

typedef struct {
	wp_pdu_type	type:8;
	union {
		ax25_address	call;
		unsigned char	status;
		wp_t			wp;
		char			string[85];
		vector_t		vector;
		list_req_t		list_req;
		list_rsp_t		list_rsp;
		info_t			info;
		info_rsp_t		info_rsp;
	} data;
} wp_pdu;

#define WP_OK			0
#define WP_INVALID_COMMAND	1
#define WP_SET_ERROR		2
#define WP_GET_ERROR		3

#define FILE_SIGNATURE	"FPACWP_V003"

typedef struct {
	char	signature[16];
	int	nb_record;
} wp_header;

/* Public fonctions provided by libwp.a */

int strmatch(char *string, char *pattern);
int wp_check_call(const char *callsign);
int wp_get(ax25_address *call, wp_t *wp);
int wp_listen(void);
int wp_open(char *);
int wp_is_open(void);
int wp_open_remote(char *source_call, struct full_sockaddr_rose *remote, int non_block);
int wp_receive_pdu(int s, wp_pdu *pdu);
int wp_search(ax25_address *call, struct full_sockaddr_rose *addr);
int wp_send_pdu(int s, wp_pdu *pdu);
int wp_set(wp_t *wp);
int wp_update_addr(struct full_sockaddr_rose *addr);
int wp_get_list(wp_t **wp, int *nb, int flags, char *mask);
int wp_is_node(char *callsign);
void my_date(char *buf, time_t date );

int wp_nb_records(void);

void dump_rose(char *, struct full_sockaddr_rose *);
void wp_close(void);
void wp_flush_pdu(void);
void wp_free_list(wp_t **wp);

#endif /* __WP_H */
