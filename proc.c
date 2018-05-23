#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "traps.h"
#include "ptable.h"

// we use ptable structure many time so declare at "ptable.h" apart
/*struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;*/

static struct proc *initproc;

int nextpid = 1;
int next_tid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// part of orininal scheduler that change context
// we declare it apart to use when i want
void
ChangeContext(struct cpu* c, struct proc* p) {
	c->proc = p;
	switchuvm(p);
	p->state = RUNNING;
//	cprintf("in scheduler, we choose this proc %s tid is %d\n",p->name,p->tid);

	swtch(&(c->scheduler), p->context);
	switchkvm();

	c->proc = 0;
}

// push_queue push the proc to the queue of MLFQ
// search empty space from the head, and push
void
push_queue(struct proc* p, int lev) {
 	
 	int mlfq_time_quantum[3] = {1, 2, 4};		// time quantum at every level

	for(int i = 0; i < NPROC; i++){
		if(ptable.mlfq[lev][i] == 0) {
			p->q_lev = lev;						// change proc information where proc exist in queue
			p->q_index = i;
			p->time_allotment = 0;				// initialize the variable that check ticks for each level of queue
			p->sh = mlfq_time_quantum[lev];		// to MLFQ, share means time quantum
			ptable.mlfq[lev][i] = p;			// push!
			break;
		}
	}
}

// pop_queue pop the proc from the queue
void
pop_queue(struct proc* p) {
	ptable.mlfq[p->q_lev][p->q_index] = 0;
	p->q_lev = 0;
	p->q_index = 0;
}

// to supervise all proc of stride
// we also use queue system only for stride
// it will be searched first of all.
void
push_stride(struct proc* p) {
	for(int i=0; i < NPROC; i++) {
		if(ptable.stride[i] == 0) {
			ptable.isNewStride = 1;					// information that new proc come in to stride scheduler			
			ptable.isNewbieComing = 0;
			p->q_lev = 0;
			p->q_index = i;
			p->isStride = 1;						// isStride is the information whether process is stride
			ptable.stride[i] = p;
			return;
		}
	}
	cprintf("no more process become stride!\n");
}

// pop the process from the stride scheduler
void
pop_stride(struct proc* p) {
	ptable.stride[p->q_index] = 0;
	p->q_index = 0;
	p->isStride = 0;
}

// check that the queue of given level is empty
// if it is not empty,  we usually need to schedule that level again
// we can see how use this function at scheduler(void)
int
isQueueEmpty(int lev) {
	int index;
	struct proc* p;
	for(index = 0; index < NPROC; index++) {
		if(ptable.mlfq[lev][index] != 0) {		// check the queue of level given empty
			p = ptable.mlfq[lev][index];		
			if((p->state == RUNNABLE) || (p->state == RUNNING)) {
				if(p->sh > 0) return 1;			// if proc's share is lower than 0 we don't need to run that proc
			}
		}
	}
	return 0;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->tid = next_tid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->num_thread = 0;
  p->isThread = 0;
  // initialize proc's tick information and sh
  // first allocation, push to the MLFQ first that level 0, highest priority
  p->rtick = 0;
  p->sh = 0;
  acquire(&ptable.lock);
  push_queue(p, 0);
  ptable.isNewbieComing = 1;	// this information ensure process to run first of all
  release(&ptable.lock);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  
  cprintf("userinit is progressing\n");
//  p->sz = (uint*)malloc(sizeof(uint));
  if((p->sz = (uint *)kalloc())==0)
    panic("proc sz memory allocation fail");
  *(p->sz) = PGSIZE;
  cprintf("userinit : pid is %d, proc sz is %d\n",p->pid, *p->sz);
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = *(curproc->sz);
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  *(curproc->sz) = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  uint sz;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  sz = *(curproc->sz);
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = (uint*)kalloc();
  np->usz = curproc->usz;
  *np->sz = *curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);
  // initialize the queue that process has occupied
  if(curproc->isStride) {
   	if(curproc->isThread)
	 	curproc->parent->sh += curproc->sh;
	else
	 	total_share -= curproc->sh;
	pop_stride(curproc);
  }
  else
   	pop_queue(curproc);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // if main thread call exit before child thread exit
  if(curproc->num_thread) {
  	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++) {
		if(p->parent == curproc)
		 	p->killed = 1;
	}
  }
// if exit called by child thread
  else if(curproc->isThread){
  	if(curproc->parent){
	 	curproc->parent->killed = 1;
	}
  }
// Pass abandoned children to init.
  else{
  	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
   		if(p->parent == curproc){
		 	cprintf("in exit, tid is %d\n",curproc->tid);
      		p->parent = initproc;
      		if(p->state == ZOMBIE)
        		wakeup1(initproc);
    	}
  	}
  }
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
		if(p->isThread)
			kfree((char*)(p->sz));
		else
		 	freevm(p->pgdir);
		p->sz = 0;
		p->usz = 0;
        p->kstack = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  int lev = 0;										// mlfq lev(lower number, higher priority)
  int qhead = 0;									// index for searching every queue
  int mlfq_time_allotment[3] = {5, 10, 100};		// time allotment at each level of queue

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
schedule:
	// stride scheduling start first to ensure their share
	for(qhead = 0; qhead < NPROC; qhead++) {
		if(ptable.stride[qhead] != 0) {						// check whether stride queue is empty
			p = ptable.stride[qhead];
			if(p->state != RUNNABLE) continue;
			if(p->rtick_for_boost >= p->sh) continue;		// if proc use up their share, pass over.
			ChangeContext(c,p);						// ChangeContext change scheduler context
		}											// to new proc context that we choose
	}
	// mlfq scheduling start
	for(lev = 0; lev < 3; lev++){
		for(qhead=0; qhead < NPROC; qhead++) {

			// if new process come(mlfq), we restart loop
		 	// to execute the new process(level 0)
		 	if(ptable.isNewbieComing != 0) {
				ptable.isNewbieComing = 0;
				lev = -1;
				qhead = -1;
				continue;
			}

			// if process use set_cpu_share, process change to stride
			// and we need to run stride first, so go to stride
			if(ptable.isNewStride != 0) {
				ptable.isNewStride = 0;
				goto schedule;
			}
			// search queue of MLFQ to find proc that can execute
		 	if(ptable.mlfq[lev][qhead] != 0) {
			 	p = ptable.mlfq[lev][qhead];

				if(p->state != RUNNABLE) continue;

				// priority down (lev change)
				if(((p->time_allotment) >= mlfq_time_allotment[lev]) && (lev<2)) {
				 	
				 	// pop p from present level queue
				 	pop_queue(p);
					// push p to lower priority queue
					push_queue(p,lev+1);

					// If we search a queue all over, check whether that queue is empty
					// Not empty, we should run that proc first before lower the lev to ensure priority
					if((qhead == (NPROC-1)) && isQueueEmpty(lev)) {
						qhead = -1;
					}
					continue;
				}
				// no new proc, no stride, no priority down, then change context to that proc
				ChangeContext(c,p);
			}
			// check that the queue is empty at every end of queue
			if((qhead == (NPROC -1)) && isQueueEmpty(lev)) {
				qhead = -1;
			}
			
	
	// this is the original part of scheduler
	// I declare ChangeContext , to use this every time i want

	/*
	// Switch to chosen process.  It is the process's job
	// to release ptable.lock and then reacquire it
	// before jumping back to us.
	c->proc = p;
	switchuvm(p);
	p->state = RUNNING;
	  
	// Count each process's tick and total tick.

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;*/
		}
	}
	release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
//  cprintf("in yield, proc's tid is %d\n",myproc()->tid);
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){ 
	  p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
set_cpu_share(int share)
{
  struct proc* p;					// There are something problems that set_cpu_share is called twice
    								// when process call syscall set_cpu_share.
  p = myproc();						// Each time total share added, the check of total share is not correctly working.

  if(!p->isStride) {					// This 'if' condition check whether it is first call. We need only one check 
										// for one process.

	if(share > (80-total_share)) {					// If total share over 80%, we kill the last process to prevent unfairness.
	  cprintf("!!!process over the limit share!!!\n");
	  kill(p->pid);
	  return -1;
	}
    total_share += share;
	//cprintf("total share %d  set share %d\n",total_share, share);
	p->sh = share;
	pop_queue(p);
	push_stride(p);
  }
  else {											// if the proc already stride, and want to change their share midstream
	total_share -= p->sh;							// subtract proc's original share from total share and add new share

   	if(share > (80-total_share)) {
		cprintf("!!!process over the limit share!!!\n");
		total_share += p->sh;						// it will be reduced at the exit();
		kill(p->pid);
		return -1;
	}							
	total_share += share;
	p->sh = share;									// it is already in the stride scheduler, so we don't need to push.
  }
  return 0;
}

int
alarm(char *proc_name) {						// it is the syscall used when i test my scheduler working
 	int i = 0;									// it gives to process name (proc->name[])
	struct proc* p;
	p = myproc();
	for(;i < 15; i++) {
		p->name[i] = proc_name[i];
	}
	/*cprintf("proc name is %s\n",proc_name);
	cprintf("myrpoc name is %s\n",p->name);*/
	return 0;
}

int
thread_create(thread_t* thread, void* (*start_routine)(void*), void* arg)
{
	int i;
	int origin_sh = 0;
 	struct proc* curproc = myproc();
	struct proc* np;
	struct proc* p;
	uint sz, sp, ustack[2];
//	when allocproc, we call kalloc() for new thread's kstack
//	then why we need ustack ? 
//	do we just alloc in user mode for user stack and change the proc's stack pointer?
	if((np=allocproc()) == 0) return -1;
	nextpid--;
	np->pid = curproc->pid;
	curproc->num_thread ++;
	*thread = np->tid;
	np->pgdir = curproc->pgdir;
	np->sz = curproc->sz;
	*(np->tf) = *(curproc->tf);
	np->parent = curproc;
//	cprintf("curproc has %d thread\n",curproc->num_thread);

//	thread already use main thread's memory
//	don't need to allocuvm newly
//	just grow the memory for new user stack of thread


//	cprintf("before allocuvm sz is %d\n",sz);
//	if((sz = allocuvm(curproc->pgdir, sz, sz+2*PGSIZE)) == 0){
//		cprintf("fuck error first\n");
//		goto bad;
//	}
	sz = curproc->usz + (uint)2*PGSIZE*(curproc->num_thread-1);
	sz = PGROUNDUP(sz);
//	if((sz = allocuvm(curproc->pgdir, sz, sz + 2*PGSIZE)) == 0){
//	 	cprintf("over the page 4gb?\n");
//	 	goto bad;
//	}
	sz = sz + (uint)2*PGSIZE;
//	clearpteu(np->pgdir, (char*)(sz - 2*PGSIZE));
	sp = sz;
	np->usz = sz;
//	cprintf("sp is %d\n",sp);
	
	ustack[0] = 0xffffffff;  // fake return PC
  	ustack[1] = (uint)arg;
	
	sp -= 8;
  	if(copyout(np->pgdir, sp, ustack, 8) < 0){
		cprintf("copyout prob at second\n");
		goto bad;
	}


	np->tf->eax = 0;
	np->tf->esp = sp;
	np->tf->eip = (uint)(start_routine);
	np->isThread = 1;
	for(i = 0; i < NOFILE; i++)
		if(curproc->ofile[i])
			np->ofile[i] = filedup(curproc->ofile[i]);
	np->cwd = idup(curproc->cwd);

	safestrcpy(np->name, curproc->name, sizeof(curproc->name));
	
	// Scheduling part	

	// if main thread is stride
	// we need to devide the share
	if(curproc->isStride) {
	 	acquire(&ptable.lock);
		np->isStride = 1;

		if(np->parent->num_thread > 1) {
			for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
				if(p->parent == curproc){
					origin_sh = (p->sh)*(curproc->num_thread-1) + curproc->sh;
					p->sh = origin_sh/(curproc->num_thread);
				}
			}
			np->sh = origin_sh/(curproc->num_thread);
			curproc->sh = np->sh + origin_sh%2;
			np->state = RUNNABLE;
		}
		else{
			np->sh = (curproc->sh)/2;
			curproc->sh = np->sh + (curproc->sh)%2;
			np->state = RUNNABLE;
		}
		pop_queue(np);
		push_stride(np);
		release(&ptable.lock);
		return 0;
	}
	// if main thread is mlfq, we just run it
	// by allocproc(), already thread is in the mlfq.
	acquire(&ptable.lock);
	
	np->state = RUNNABLE;
	
	release(&ptable.lock);
	
//	cprintf("thread tid is %d, sz is %d, sp is %d\n",np->tid,*np->sz,np->tf->esp);
//	cprintf("curproc's main thread's tid is %d\n",curproc->tid);
	
	return 0;

  bad:
	panic("create panic!\n");
		
}

int
thread_join(thread_t thread, void** retval)
{
	struct proc *p;
	int havekids;
//	int tid;
	struct proc *curproc = myproc();
//	cprintf("at join curproc tid is %d\n",curproc->tid);
	acquire(&ptable.lock);
	for(;;){
	// Scan through table looking for exited children.
		havekids = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		  if(p->tid != thread)
			continue;

		  // child thread change to main thread
		  // for example, by exec.
		  // then this thread will be initialized by wait.
		  if(p->isThread == 0){
		   	cprintf("exec test fuck\n");
			release(&ptable.lock);
			return 0;
		  }
		   	
		  havekids = 1;
		  if(p->state == ZOMBIE){
			// Found one.
		   	*retval =(void*)p->result;
			//if(!deallocuvm(p->pgdir, p->usz, p->usz - 2*PGSIZE))
			//	panic("dealloc fail!\n");
			kfree(p->kstack);
			p->kstack = 0;
			//freevm(p->pgdir);
			p->pgdir = 0;
			p->sz = 0;
			p->usz = 0;
			p->pid = 0;
			p->tid = 0;
			curproc->num_thread --;
//			if(curproc->num_thread == 0)
//				*(curproc->sz) = curproc->usz;
			// thread return share to main thread when thread_exit
			// things about scheduling already initialized in exit
			p->parent = 0;
			p->name[0] = 0;
			p->killed = 0;
			p->state = UNUSED;
			release(&ptable.lock);
			return 0;
		  }
		}
		if(!havekids || curproc->killed) {
			release(&ptable.lock);
			return -1;
		}

		sleep(curproc, &ptable.lock);
	}

}

void
thread_exit(void *retval)
{
  struct proc *curproc = myproc();
//  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  curproc->result = (int)retval;
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);
  // initialize the queue that process has occupied
  if(curproc->isStride != 0) {
   	curproc->parent->sh += curproc->sh;
	curproc->sh = 0;
	pop_stride(curproc);
  }
  else
   	pop_queue(curproc);

  // maybe we don't need free the sz of proc?
  // we just copy the address of main thread's 
  // so we just initialize it to zero?
  // that work is done in join();

//  kfree((char *)curproc->sz);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  // Pass abandoned children to init.
/*  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }*/

//  cprintf("is it fucking exit is progressing\n");
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

