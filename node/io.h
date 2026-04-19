#define EOLMODE_TEXT	0
#define EOLMODE_BINARY	1

#define AX25_EOL	"\r"
#define NETROM_EOL	AX25_EOL
#define	ROSE_EOL	AX25_EOL
#define INET_EOL	"\r\n"
#define UNSPEC_EOL	"\n"

int init_io(int fd, int paclen, char *eol);
void end_io(int fd);

int set_paclen(int fd, int paclen);
int set_eolmode(int fd, int newmode);
int set_telnetmode(int fd, int newmode);
int usflush(int fd);

int usgetc(int fd);
int usputc(unsigned char c, int fd);

char *readline(int fd);
char *usgets(char *buf, int buflen, int fd);
int usprintf(int fd, const char *fmt, ...);
int tprintf(const char *fmt, ...);
int usputs(const char *s, int fd);
int tputs(const char *s);

void tn_do_linemode(int fd);
void tn_will_echo(int fd);
void tn_wont_echo(int fd);
