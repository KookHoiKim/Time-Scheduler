struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct proc* mlfq[3][NPROC];		// queue for MLFQ
  struct proc* stride[NPROC];		// queue for stride
  int isNewbieComing;				// if isNewbieComing is 1, then new process is allocated
  int isNewStride;					// same as Newbiecome, a process call set_cpu_share,
} ptable;							// so that it turns to stride scheduling, isNewStride becomes
									// 1, so we can know that new Stride process is produced
