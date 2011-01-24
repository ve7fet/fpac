/*
 * wp/sockevent.c : Handle multiple sockets events with select system call
 *
 * FPAC project
 *
 * F1OAT 960630
 */
 
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
/*#include <sys/types.h>*/
#include <unistd.h>
#include <assert.h>

#include "ax25compat.h"
#include "sockevent.h"
#include "wp.h"

static fd_set FdSet[3];
static fd_set ActiveSet[3];
static void (*HandlerList[FD_SETSIZE])(int fd);

/*
 * Wait and event on the pool of fd registered to sockevents
 *
 * Return :	>= 0 		: fd on which an event as been received
 * 		-1   		: timeout
 *		-2	  	: select error
 */		
 
int WaitEvent(int MilliSecTimeout)
{
	int rc, fd, Set;
	struct timeval Timeout;
	int i;
	
	Timeout.tv_sec = MilliSecTimeout/1000;
	Timeout.tv_usec = 0;
	
	for (i=0; i<3; i++) FdSet[i] = ActiveSet[i];
	
	rc = select(FD_SETSIZE, &FdSet[0], &FdSet[1], &FdSet[2], &Timeout);
	if (rc == -1) perror("WaitEvent : select");
	if (rc == 0) wp_flush_pdu();
	if (rc <= 0) return rc-1;
	
	for (fd=0; fd<FD_SETSIZE; fd++) {
		for (Set=0; Set<3; Set++) {
			if (FD_ISSET(fd, &FdSet[Set])) return fd;
		}
	}
	
	return -2;
}

/*
 * Get event bit field for a specific fd
 *
 * Return :	Bit 0 : Read ready
 *		Bit 1 : Write ready
 *		Bit 2 : Exception
 *		 
 */
 
int GetEvent(int Fd)
{
	int i, BitField = 0;
	
	assert(Fd>=0 && Fd<FD_SETSIZE);
	for (i=0; i<3; i++) BitField |= FD_ISSET(Fd, &FdSet[i]) ? (1<<i) : 0;
	return BitField;
}

/*
 * Register an event awaited for a specific fd
 *
 * EventAwaited :	0 : Read ready
 *			1 : Write ready
 *			2 : Exception
 */
 
void RegisterEventAwaited(int Fd, int EventAwaited)
{
	assert(Fd>=0 && Fd<FD_SETSIZE);
	assert(EventAwaited>=0 && EventAwaited<=2);
	
	FD_SET(Fd, &ActiveSet[EventAwaited]);
}

/*
 * Unregister an event awaited for a specific fd
 *
 * EventAwaited :	0 : Read ready
 *			1 : Write ready
 *			2 : Exception
 */
 
void UnRegisterEventAwaited(int Fd, int EventAwaited)
{
	assert(Fd>=0 && Fd<FD_SETSIZE);
	assert(EventAwaited>=0 && EventAwaited<=2);
	
	FD_CLR(Fd, &ActiveSet[EventAwaited]);
}
  
/*
 * Register an event handler for a specific fd
 */
 
void RegisterEventHandler(int Fd,  void (*Handler)(int fd))
{
	assert(Fd>=0 && Fd<FD_SETSIZE);
	
	HandlerList[Fd] = Handler;
} 

/*
 * Process and event by calling the event handler
 */
 
void ProcessEvent(int Fd)
{
	if (HandlerList[Fd]) {
		HandlerList[Fd](Fd);
	}
}
