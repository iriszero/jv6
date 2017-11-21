#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
	int mlfq_pass, stride_pass, global_pass;
	int total_stride_tickets; // <= 100-MIN_MLFQ_PERCENTAGE
	int num_thread;
} ptable;

static struct proc *initproc;
const int mlfq_ticks[NLEVEL] = {5, 10, 20};
const int stride1 = (1 << 10);

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
	initlock(&ptable.lock, "ptable");
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
	p->tid = 0;
	p->level = 0;
	p->mlfq_remain_tick = mlfq_ticks[0];
		p->stride_tickets=0;
		p->stride_current_pass=ptable.stride_pass;
		p->is_thread = 0;
		p->process = p;
		p->parent = p;
		p->scheduled = 0;

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

		return p;
	}

	static struct proc*
	allocthread(void)
	{
		struct proc *p;
		char *sp;
		int i;

		acquire(&ptable.lock);
		for (p=ptable.proc; p<&ptable.proc[NPROC]; p++)
			if (p->state == UNUSED)
				goto found;

		release(&ptable.lock);
		return 0;

	found:
		p->state = EMBRYO;
		p->pid = proc->pid;
		p->tid = nexttid++;
		p->level = 0;
		p->mlfq_remain_tick = mlfq_ticks[0];
		p->is_thread = 1;
		p->scheduled = 0;
		release(&ptable.lock);

		if ((p->kstack=kalloc()) == 0) {
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
		p->sz = PGSIZE;
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
		p->parent = p;
		p->process = p;

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
	//	cprintf("growproc\n");
		acquire(&ptable.lock);	
		sz = proc->process->sz;
		if(n > 0){
			if((sz = allocuvm(proc->process->pgdir, sz, sz + n)) == 0) {
				release(&ptable.lock);
				return -1;
			}
		} else if(n < 0){
			if((sz = deallocuvm(proc->process->pgdir, sz, sz + n)) == 0) {
				release(&ptable.lock);
				return -1;
			}
		}

		proc->process->sz = sz;
		switchuvm(proc);
		
		release(&ptable.lock);
		return 0;
	}

	// Create a new process copying p as the parent.
	// Sets up stack to return as if from system call.
	// Caller must set state of returned proc to RUNNABLE.
	int
	fork(void)
	{
		int i, pid, fd;
		struct proc *np, *nt;

	//	cprintf("fork\n");
		if (!proc->is_thread) {
			if((np = allocproc()) == 0){
				return -1;
			}
			
			if((np->pgdir = copyuvm(proc->process->pgdir, proc->process->sz)) == 0){
				kfree(np->kstack);
				np->kstack = 0;
				np->state = UNUSED;

				return -1;
			}

			for(fd = 0; fd < NOFILE; fd++) {
				if(proc->process->ofile[fd]) {
					np->ofile[fd] = filedup(proc->process->ofile[fd]);
				}
			}
			np->cwd = idup(proc->process->cwd);

			for (i=0; i<NTHREAD; i++) {
				if (proc->thread[i].allocated) {
					if ((nt = allocthread()) == 0) {
						for (;i>=0; i--) {
							kfree(nt->kstack);
							nt->kstack = 0;
							nt->state = UNUSED;
						}
						for (fd =0;fd<NOFILE; fd++) {
							if (np->ofile[fd]) {
								fileclose(np->ofile[fd]);
							}
						}
						iput(np->cwd);
						freevm(np->pgdir);
						return -1;
					}

					for (fd= 0;fd<NOFILE; fd++) {
						nt->ofile[fd] = np->ofile[fd];
					}
					nt->cwd = np->cwd;
					nt->tid = np->tid;
					nt->thread_idx = i;
					nt->process = np;
					nt->parent = np;
					np->thread[i].proc= nt;
				}

				np->thread[i].allocated = proc->process->thread[i].allocated;
				np->thread[i].stack = proc->process->thread[i].stack;
			}
			np->sz = proc->process->sz;
			np->parent = proc;
			*np->tf = *proc->tf;
			np->tf->eax = 0;
			safestrcpy(np->name, proc->name, sizeof(proc->name));
			pid = np->pid;
		}
		else {
			if ((nt=allocthread()) == 0) {
				return -1;
			}


			for (i=0; i<NTHREAD; i++) {
				if (proc->process->thread[i].allocated==0)
					continue;
				
				proc->process->thread[i].allocated = 1;
				nt->thread_idx = i;

				strncpy((char*)proc->process->thread[proc->thread_idx].stack,
						(char*)proc->process->thread[nt->thread_idx].stack,
						PGSIZE);
			}

			kfree(nt->kstack);
			nt->kstack = 0;
			nt->state = UNUSED;
			return -1;

	found:
			nt->process = proc->process;
			nt->parent = proc;
			for (fd = 0; fd<NOFILE; fd++) {
				nt->ofile[fd] = proc->process->ofile[fd];
			}
			nt->cwd = proc->process->cwd;
			*nt->tf = *proc->tf;
			nt->tf->eax = 0;
			safestrcpy(nt->name, proc->name, sizeof(proc->name));
			pid = nt->pid;
		}
		
		acquire(&ptable.lock);
		np->state = RUNNABLE;
		release(&ptable.lock);
		return pid;
	}

	// Exit the current process.  Does not return.
	// An exited process remains in the zombie state
	// until its parent calls wait() to find out it exited.
	// Terminate all LWP and a process wherever it was called from
	void
	exit(void)
	{
		struct proc *p;
		int fd;
		
		//cprintf("exit called pid = %d, tid=%d\n", proc->pid, proc->tid);
		if(proc == initproc)
			panic("init exiting");


		// Close all open files.

		
		for(fd = 0; fd < NOFILE; fd++) {
			if(proc->process->ofile[fd]) {
				fileclose(proc->process->ofile[fd]);
				proc->process->ofile[fd] = 0;
			}
		}
		
		iput(proc->process->cwd);
		proc->process->cwd = 0;

		acquire(&ptable.lock);
		// Parent might be sleeping in wait().
		wakeup1(proc->process->parent);
		proc->process->killed =1;	
		ptable.total_stride_tickets -= proc->process->stride_tickets;
		proc->process->stride_tickets = 0;
		for (p = ptable.proc; p<&ptable.proc[NPROC]; p++) {
			if (p->state == UNUSED || p->state == EMBRYO)
				continue;
			if (p==initproc || p == proc)
				continue;
			
			// Process
			if (p->is_thread == 0) {
				// Itself
				if (p->parent == proc->process) {
					p->parent = initproc;
						//cprintf("pid = %d, tid = %d attached to initproc\n", p->pid, p->tid);
					if (p->state == ZOMBIE)
						wakeup1(initproc);
				}
			}
				
			// Thread
			if(p->is_thread && p->process == proc->process) {
				for (fd = 0; fd < NOFILE; fd++) {
					p->ofile[fd] = 0;
				}
				//iput(p->cwd);
				p->cwd = 0;
				
				//cprintf("pid = %d, tid = %d killed\n", p->pid, p->tid);
				p->state = UNUSED;
				kfree(p->kstack);	
				p->kstack = 0;
			}
		}					
		
		// Itself
		if (proc->is_thread == 0) {
			// Process
			p->state = UNUSED;
		}
		else {
			// Thread
			for (fd = 0; fd< NOFILE; fd++) {
				proc->ofile[fd] = 0;
			}
			proc->cwd = 0;
			
			proc->process->state = UNUSED;
			kfree(proc->process->kstack);
			proc->process->kstack = 0;
		}


		// Jump into the scheduler, never to return.
		proc->state = ZOMBIE;
		proc->is_thread = 0; // pretend to be a process for wait().
		proc->parent = proc->process->parent; //pretend to be a process for wait().
		proc->pgdir = proc->process->pgdir; // make sure
		proc->process->scheduled = 0;
		sched();
		panic("zombie exit");
	}

	// Wait for a child process to exit and return its pid.
	// Return -1 if this process has no children.
	int
	wait(void)
	{
		struct proc *p, *t;
		int havekids, pid, fd;

		acquire(&ptable.lock);
		//cprintf("wait\n");
		for(;;){
			// Scan through table looking for exited children.
			havekids = 0;
			for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
				if(p->parent != proc)
					continue;
				if(p->state == UNUSED || p->state == EMBRYO)
					continue;
		
				havekids = 1;
				if(p->state == ZOMBIE){
					// Found one.
					pid = p->pid;
					//cprintf("pid = %d, tid = %d killed in wait\n", pid, p->tid);
					// Only free pgdir if it is process (main thread)
					
					for (t = ptable.proc; t<&ptable.proc[NPROC]; t++) {
						if ( t->state == UNUSED)
							continue;
						if (t->is_thread && t->process == p) {
							//cprintf("pid = %d, tid = %d killed in wait\n", t->pid, t->tid);
							kfree(t->kstack);
							t->state = UNUSED;
							for (fd = 0; fd<NOFILE; fd++) {
								t->ofile[fd] = 0;
							}
							t->cwd = 0;
						}
					}

					freevm(p->pgdir);
					kfree(p->kstack);
					p->pgdir= 0;
					p->kstack = 0;
					p->pid = 0;
					p->tid = 0;
					p->parent = 0;
					p->name[0] = 0;
					p->killed = 0;
					p->state = UNUSED;
					p->level = 0;
					p->mlfq_remain_tick=0;
					ptable.total_stride_tickets-=p->process->stride_tickets;
					p->process->stride_tickets=0;
					p->process->stride_current_pass=0;
					release(&ptable.lock);

					return pid;
				}
			}

			// No point waiting if we don't have any children.
			if(!havekids || proc->killed){
				release(&ptable.lock);
				return -1;
			}

			// Wait for children to exit.  (See wakeup1 call in proc_exit.)
			sleep(proc, &ptable.lock);  //DOC: wait-sleep
		}
	}

	struct proc*
	mlfq_scheduler(void)
	{
		int i; struct proc *p;

		ptable.mlfq_pass +=	stride1 / MIN_MLFQ_PERCENTAGE;
		ptable.global_pass +=
			stride1 / (MIN_MLFQ_PERCENTAGE + ptable.total_stride_tickets);

	//	for(;;) {
			for (i=0; i< NLEVEL; i++) {
				for (p = ptable.proc; p< &ptable.proc[NPROC]; p++) {
					if (p->state == RUNNABLE && p->level==i && p->mlfq_remain_tick >0) {
						p->mlfq_remain_tick--;
						if (p->mlfq_remain_tick == 0) {
							if (p->level<NLEVEL-1) {
								p->level++;
							}
							p->mlfq_remain_tick = mlfq_ticks[p->level];
						}
						return p;
					}
				}
			}
	//	}
		return 0;
	}

	struct proc*
	stride_scheduler(void)
	{
		struct proc* p;
		
		//for(;;) {	
			ptable.stride_pass += stride1 / ptable.total_stride_tickets;
			ptable.global_pass +=
				stride1 / (MIN_MLFQ_PERCENTAGE + ptable.total_stride_tickets);

			for (p = ptable.proc; p<&ptable.proc[NPROC]; p++) {
				if (p->state == RUNNABLE && p->process->stride_current_pass < ptable.stride_pass && p->process->stride_tickets > 0) {
					p->process->stride_current_pass += stride1 / p->process->stride_tickets;
					return p;
				}
			}
		//}
		return 0;
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
	_scheduler(void)
	{
		struct proc *p;
		for(;;) {
			sti();
			acquire(&ptable.lock);
			for (p=ptable.proc; p<&ptable.proc[NPROC]; p++) {
				if (p->state != RUNNABLE || p->process->scheduled)
					continue;

				p->pgdir = p->process->pgdir;
				p->sz = p->process->sz;
				p->process->scheduled = 1;
				proc = p;
				
				//cprintf("{SCHED} pid %d tid %d pgdir %x sz %x kstack %x\n", proc->pid, proc->tid, proc->pgdir, proc->sz, proc->kstack);
				switchuvm(p);
				p->state = RUNNING;
				swtch(&cpu->scheduler, proc->context);
				switchkvm();
				proc=0;
			}
			release(&ptable.lock);
		}
	}
		
		
	void
	scheduler(void)
	{
		struct proc *p;

		for(;;){
			// Enable interrupts on this processor.
			sti();

			// Loop over process table looking for process to run.
			acquire(&ptable.lock);
			if (ptable.total_stride_tickets > 0) {
				if (ptable.mlfq_pass > ptable.stride_pass) {
					p = stride_scheduler();
				}
				else {
					p = mlfq_scheduler();
				}
			}
			else {
				p = mlfq_scheduler();
				ptable.stride_pass = ptable.mlfq_pass;
			}
			if (!p) {
				release(&ptable.lock);
				continue;
			}
			// Switch to chosen process.  It is the process's job
			// to release ptable.lock and then reacquire it
			// before jumping back to us.
			proc = p;
			p->sz = p->process->sz;
			p->pgdir = p->process->pgdir;
			p->process->scheduled = 1;

			switchuvm(p);
			p->state = RUNNING;
			swtch(&cpu->scheduler, p->context);
			switchkvm();
			
			// Process is done running for now.
			// It should have changed its p->state before coming back.

			proc = 0;
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

		if(!holding(&ptable.lock))
			panic("sched ptable.lock");
		if(cpu->ncli != 1)
			panic("sched locks");
		if(proc->state == RUNNING)
			panic("sched running");
		if(readeflags()&FL_IF)
			panic("sched interruptible");
		intena = cpu->intena;
		swtch(&proc->context, cpu->scheduler);
		cpu->intena = intena;
	}

	// Give up the CPU for one scheduling round.
	void
	yield(void)
	{
		acquire(&ptable.lock);  //DOC: yieldlock
		proc->state = RUNNABLE;
		proc->process->scheduled = 0;
		sched();
		release(&ptable.lock);
	}

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
		if(proc == 0)
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
		proc->chan = chan;
		proc->state = SLEEPING;
		proc->process->scheduled = 0;
		sched();

		// Tidy up.
		proc->chan = 0;

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
		//cprintf("kill function called\n");
		acquire(&ptable.lock);
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->pid == pid && p->is_thread == 0){
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

	void
	boost_up(void)
	{
		struct proc *p;
		acquire(&ptable.lock);
		for (p=ptable.proc; p<&ptable.proc[NPROC]; p++) {
			p->level=0;
			p->mlfq_remain_tick=mlfq_ticks[0];
		}
		release(&ptable.lock);
	}

int
set_cpu_share(int ticket_amount)
{
  acquire(&ptable.lock);
  int ticket_issued= ticket_amount;
  if (ptable.total_stride_tickets + ticket_amount > 100-MIN_MLFQ_PERCENTAGE)
  {
    ticket_issued = 100 - MIN_MLFQ_PERCENTAGE - ptable.total_stride_tickets;
  }
	proc->process->stride_tickets = ticket_issued;
	proc->process->stride_current_pass = ptable.stride_pass;
	ptable.total_stride_tickets += ticket_issued;
  release(&ptable.lock);
  return ticket_issued;
}

int
thread_create(thread_t *thread, void* (*start_routine)(void *), void *arg)
{
	int i;
	struct proc *np;
	uint ustack[2];  
	
	// Allocate process.
	if ( (np=allocthread()) == 0) {
		return -1;
	}

	// Allocate two pages at the next page boundary.
	// Make the first inaccessible.	Use the second as the thread stack
	//clearpteu(proc->pgdir, (char*)(proc->sz - PGSIZE));
	
	
	acquire(&ptable.lock);
	for (i=0; i<NOFILE; i++) {
		np->ofile[i] = proc->ofile[i];
	}
	np->cwd = proc->cwd;
	
	safestrcpy(np->name, proc->name, sizeof(proc->name));
	

	np->thread_idx = -1;
	for (i=0; i< NTHREAD; i++) {
		if (proc->process->thread[i].allocated == 0) {
			proc->process->thread[i].allocated = 1;
			np->thread_idx = i;
			break;
		}
	}
	if (np->thread_idx == -1) {
		kfree(np->kstack);
		np->state = UNUSED;
		release(&ptable.lock);
		return -1;
	}

	//np->pgdir = proc->pgdir;
	np->sz = proc->sz;
	np->parent = proc;
	np->pgdir = proc->pgdir;
	np->process = proc->process;
	*np->tf = *proc->tf;
	np->tf->esp = np->process->thread[np->thread_idx].stack + PGSIZE;
	
	// fake return address
	ustack[0] = 0xffffffff;
	ustack[1] = (uint)arg;
	np->tf->esp -= (2)*4;

	if (copyout(np->process->pgdir, np->tf->esp, ustack, (2)*4) < 0) {
		cprintf("thread_create copyout failed\n");
		proc->process->thread[np->thread_idx].allocated = 0;
		kfree(np->kstack);
		np->kstack = 0;
		np->state = UNUSED;
		release(&ptable.lock);
		return -1;
	}
	
	np->tf->eax = 0;
	np->tf->ebp = np->tf->esp;
	np->tf->eip = (uint)start_routine;
	np->state = RUNNABLE;
	*thread = np->tid;

	//cprintf("pid=%d, tid=%d thread created\n", np->pid, np->tid);
	release(&ptable.lock);

	return 0;
}

void
thread_exit(void *retval)
{
	int fd;

	//cprintf("thread exit called\n");
	if (!proc->is_thread) {
		return;
	}
	for (fd = 0; fd<NOFILE; fd++) {
		proc->ofile[fd] = 0;
	}
	proc->cwd = 0;

	acquire(&ptable.lock);

	proc->tf->eax = (uint)retval;
		
	wakeup1(proc->parent);
	proc->state = ZOMBIE;
	proc->process->scheduled = 0;
	sched();
	panic("zombie thread exit");
}	

int
thread_join(thread_t thread, void **retval)
{
	struct proc *p;
	int havethreads, fd;
	
	acquire(&ptable.lock);
	//cprintf("thread_join tid=%d\n", thread);
	for (;;) {
		havethreads = 0;

		for (p=ptable.proc; p<&ptable.proc[NPROC]; p++) {
			// Not LWP of parent
			if (!p->is_thread || p->tid!=thread)
				continue;
			if (p->state == UNUSED || p->state == EMBRYO)
				continue;

			havethreads = 1;

			if (p->state == ZOMBIE) {
				//cprintf("thread killed in thread_join tid=%d\n", p->tid);
				*retval = p->tf->eax;
				

				if (0<=p->thread_idx && p->thread_idx < NTHREAD)
					p->process->thread[p->thread_idx].allocated = 0;
				
				for (fd = 0; fd< NOFILE; fd++) {
					if (p->ofile[fd]) {
						p->ofile[fd] = 0;
					}
				}
				p->cwd = 0;

				kfree(p->kstack);
				p->kstack = 0;
				p->pid = 0;
				p->tid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED;
				p->level = 0;
				p->mlfq_remain_tick = 0;
				p->process = 0;	
				p->is_thread = 0;
				p->state = UNUSED;	
				
				release(&ptable.lock);
				return 0;
			}
		}
		
		// No joinable threads.
		if (!havethreads || proc->killed) {
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.
		sleep(proc, &ptable.lock);
	}
}
