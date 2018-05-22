#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "ptable.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

// Boost all process's ticks and boost tick
void
boost(void)
{
  struct proc* p;
  acquire(&ptable.lock);
  // init every queue from here, especially ticks
  total_share = 0;
  //cprintf("**************BOOST TIME*****************\n");
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNING || p->state == RUNNABLE) {
	  //cprintf("%s running tick : %d, share : %d, isStride %d\n", p->name, p->rtick_for_boost, p->sh,p->isStride);
	  p->rtick = 0;
	  p->rtick_for_boost = 0;
	  p->time_allotment = 0;
	  
	  if(p->isStride) total_share += p->sh;
  	}
  }
  // init MLFQ's queue, change their order
  for(int lev = 0; lev<3; lev++) {
  	for(int i=NPROC-1; i>=0;i--) {		// from here, 'i' start from the tail
		if(ptable.mlfq[lev][i] != 0) {	// this prevents the unfairness between MLFQ proc.
			p = ptable.mlfq[lev][i];	// the small loss of last proc will be very large 
			pop_queue(p);				// if time goes on(if processes running time long)
			// every MLFQ proc boost up to lev 0
			push_queue(p,0);			
		}
	}
  }
  ptable.isNewStride = 1;				// to start scheduler from the stride scheduler
  //cprintf("****************************************\n");
  release(&ptable.lock);
}  

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
	  wakeup(&ticks);
      release(&tickslock);

	  // boosting here
	  // every 100tick, we do boosting

	  if(ticks % 100 == 0) {
	  	boost();
	  }
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s tid %d: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, myproc()->tid, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER) {
  	myproc()->rtick_for_boost++;	// we check the time when we do boosting
   	myproc()->rtick++;				// rtick is for check that should we yield or not
	myproc()->time_allotment++;		// allotment check for MLFQ proc that should drop the priority
	if(myproc()->rtick >= myproc()->sh) {
		myproc()->rtick = 0;		// if yield, initialize rtick for the next
//		cprintf("in trap.c, print just before yield %s\n",myproc()->name);
		yield();
	}						// detail description of variables is at wiki or declalation 
  }							// of struct proc

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
