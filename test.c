#include "types.h"
#include "stat.h"
#include "user.h"


int main(int argc, char *argv[]){
	int pid;
	int	ppid;
	pid = getpid();
	ppid = getppid();
	printf(1,"%d is present process id and %d is parent process\n",pid,ppid);
	exit();
}
