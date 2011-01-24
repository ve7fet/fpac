#ifndef SYSINFO_H
#define SYSINFO_H

int        loadavg(double *av1, double *av5, double *av15);
int        uptime (double *uptime_secs, double *idle_secs);

char *meminfo(void);

unsigned read_total_main(void);

#endif /* SYSINFO_H */
