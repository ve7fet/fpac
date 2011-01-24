/*
 *
 * extcmd.c
 * FPAC project 
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
/*#include <fcntl.h>*/
#include <errno.h>
#include <time.h>
#include <sys/types.h>
/*#include <sys/wait.h>*/
/*#include <sys/socket.h>*/

#include "node.h"
#include "io.h"

#include "ax25compat.h"

#define ECMD_PIPE	1		/* Run through pipe 		*/
#define ECMD_RECONN	2		/* */

static int norm_extcmd(struct cmd *cmdp, char **argv)
{
	int pid;

	alarm(0L);
	pid = fork();
	if (pid == -1) {
		/* fork error */
		node_perror("norm_extcmd: fork", errno);
		return 0;
	}
	if (pid == 0) 
	{
		/* child */
		setgroups(0, NULL);
		/*
		setgid(cmdp->gid);
		setuid(cmdp->uid);
		*/
		execvp(argv[0], argv);
		node_perror("norm_extcmd: execve", errno);
		exit(1);
	}
	/* parent */
	waitpid(pid, NULL, 0);
	return 0;
}

int extcmd(struct cmd *cmdp, char **argv)
{
	int ret;

	User.state = STATE_EXTCMD;
	User.dl_type = AF_UNSPEC;
	strcpy(User.dl_name, cmdp->name);
	strupr(User.dl_name);
	update_user();

 	ret = norm_extcmd(cmdp, argv);

 	return ret;
}
