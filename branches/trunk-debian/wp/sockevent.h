int WaitEvent(int MilliSecTimeout);
int GetEvent(int Fd);
void RegisterEventAwaited(int Fd, int EventAwaited);
void UnRegisterEventAwaited(int Fd, int EventAwaited);
void RegisterEventHandler(int Fd, void (*Handler)(int fd));
void ProcessEvent(int Fd);

#define READ_EVENT	0
#define WRITE_EVENT	1
#define	EXCEPT_EVENT	2

  
