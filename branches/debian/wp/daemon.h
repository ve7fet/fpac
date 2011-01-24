/*
 * Function to convert a running process into a "proper" daemon.
 * 
 * Original code from Jonathan Naylor G4KLX
 */
 
#ifndef _DAEMON_H
#define	_DAEMON_H

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The argument is whether to ignore the death of child processes. The function
 * return non-zero if all was OK, else zero if there was a problem. 
 */
extern int daemon_start(int);

#ifdef __cplusplus
}
#endif

#endif
