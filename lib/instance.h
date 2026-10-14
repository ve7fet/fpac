/* Only one copy of each FPAC daemon (lib/instance.c) */
#ifndef FPAC_INSTANCE_H
#define FPAC_INSTANCE_H

extern int fpac_single_instance(const char *prog);
extern void fpac_single_instance_pid(const char *prog);
extern void fpac_single_instance_close(void);

#endif
