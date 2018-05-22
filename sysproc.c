#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}
int
sys_getppid(void){
	return myproc()->parent->pid;
}
int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = *(myproc()->sz);
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// syscall for set_cpu_share
int
sys_set_cpu_share(void)
{
  int share;
  if (argint(0, &share) < 0)
    return -1;

  return set_cpu_share(share);
}

int
sys_alarm(void)
{
  //int i=0;
  char* proc_name;
  
  if(argptr(1,(char**)&proc_name, 1) < 0)
    return -1;
	
 // cprintf("%d\n",proc_name);
  return alarm(proc_name);
}

// syscall yield
int
sys_yield(void)
{
  yield();
  return 0;
}

// syscall that return the process's present level of queue.
int
sys_getlev(void)
{
	return myproc()->q_lev;
}

// syscall that create thread
int
sys_thread_create(void) 
{
/*	thread_t * thread;
	void (*start_routine)(void*);
	void* arg; 
	if(argptr(1,(char**)&thread,1) < 0) return -1;
	if(argptr(1,(char**)&start_routine, 1) < 0) return -1;
	if(argptr(1,(char**)&arg, 1) < 0)	return -1;
//	cprintf("%d is syscall fnc address\n",(int)start_routine);
	return thread_create(thread, *start_routine, arg);
	*/
 	int n;
	void * thread;
	void * start_routine;
	void * arg;
	
	if(argint(0, &n) < 0)
		return -1;
	thread = (void*) n;
	if(argint(1, &n) < 0)
	 	return -1;
	start_routine = (void*) n;
	if(argint(2, &n) < 0)
		return -1;
	arg = (void*) n;
	return thread_create(thread, start_routine, arg);

}

// syscall that wait until the thread end
int
sys_thread_join(void)
{
 	int n;
	thread_t thread;
	void** retval;
	if(argint(0, &n) < 0)	return -1;
	thread = (thread_t) n;
	if(argint(1, &n) < 0)	return -1;
	retval = (void**) n;
	return thread_join(thread, retval);
}

// syscall that exit thread
int
sys_thread_exit(void)
{
 	int n;
 	void* retval;
	if(argint(0,&n) < 0) return -1;
	retval = (void*) n;
	thread_exit(retval);
	return 0; // not reached
}

