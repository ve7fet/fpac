extern wp_header *db_header;	/* Database file header */
extern wp_t *db_records;	/* Database file records */

int db_list_get(list_req_t *req, wp_t **wp, int *nb);
void db_list_free(wp_t **wp);
int db_get(ax25_address *call, wp_t *wp);
int db_set(wp_t *wp, int force);
void db_compute_vector(int dirty, vector_t *vector);
int db_open(char *file, wp_t *wp);
void db_close(void);
void db_info(struct wp_info *wpi);
