#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#ifdef CS333_P2
#include "uproc.h"
#endif //CS333_P2

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};


#ifdef CS333_P3
struct ptrs {
struct proc * head;
struct proc * tail;
};
#endif //CS333_P3

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  #ifdef CS333_P3
  struct ptrs list[statecount]; //array of lists
  #endif //CS333_P3
  #ifdef CS333_P4
  struct ptrs ready[MAXPRIO+1]; //array of runnable processes
  uint PromoteAtTime;
  #endif //CS333_P4
} ptable;

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void assertState(struct proc*, enum procstate);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void stateTransition(enum procstate, enum procstate, struct proc* p);
#endif
#ifdef CS333_P4
static void listTransition(struct ptrs*, struct ptrs*, enum procstate, enum procstate, struct proc*);
static void promoteAll();
#endif //CS333_P4
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
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
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
  #ifdef CS333_P3
  p = ptable.list[UNUSED].head;
  if (p)
    stateTransition(UNUSED, EMBRYO, p);
  else{
    release(&ptable.lock);
    return 0;
  }
  #else
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
  p->state = EMBRYO;
  #endif 

  p->pid = nextpid++;
  release(&ptable.lock);
  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif //CS333_P2

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    #ifdef CS333_P3
    acquire(&ptable.lock);
    stateTransition(EMBRYO, UNUSED, p);
    release(&ptable.lock);
    #else
    p->state = UNUSED;
    #endif
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

  #ifdef CS333_P1
  p->start_ticks = ticks;
  #endif //CS333_P1
  #ifdef CS333_P4
  p->priority = MAXPRIO; //Highest Priority
  p->budget = DEFAULT_BUDGET;
  #endif //CS333_P4
  return p;
}

// Set up first user process.
void
userinit(void)
{
  #ifdef CS333_P3
  acquire(&ptable.lock); //concurrency control
  initProcessLists();
  initFreeList();
  #ifdef CS333_P4
  ptable.PromoteAtTime = ticks+TICKS_TO_PROMOTE;
  #endif //CS333_P4
  release(&ptable.lock);
  #endif //CS333_P3

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
  #ifdef CS333_P2
  p->uid = UID;
  p->gid = GID;
  #endif //CS333_P2
  
  #ifdef CS333_P4
  acquire(&ptable.lock);
  listTransition(&ptable.list[EMBRYO], &ptable.ready[MAXPRIO], EMBRYO, RUNNABLE, p);
  release(&ptable.lock);
  
  #elif defined(CS333_P3)
  acquire(&ptable.lock);
  stateTransition(EMBRYO, RUNNABLE, p);
  release(&ptable.lock);
  #else
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  p->state = RUNNABLE;
  release(&ptable.lock);
  #endif
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
#ifdef CS333_P3
    acquire(&ptable.lock);
    stateTransition(EMBRYO, UNUSED, np);  //change state from embryo to unused
    release(&ptable.lock);
#else
    np->state = UNUSED;
#endif
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
#ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
#endif //CS333_P2
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
#ifdef CS333_P4
  listTransition(&ptable.list[EMBRYO], &ptable.ready[MAXPRIO], EMBRYO, RUNNABLE, np);
#elif defined(CS333_P3)
  stateTransition(EMBRYO, RUNNABLE, np); 
#else
  np->state = RUNNABLE;
#endif
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
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

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
/*#ifdef CS333_P4
  for(int i = 0; i <= MAXPRIO; i++){
    p = ptable.ready[i].head;
    while (p){
      if(p->parent == curproc)
        p -> parent = initproc;
    }
    p = p -> next;

  }
#endif
*/
  // Pass abandoned children to init.
  for(int i = 1; i < ZOMBIE; i++){
    p = ptable.list[i].head;
/*#ifdef CS333_P4
    if (p-> state == RUNNABLE)
      continue;
#endif
*/    while (p){
      if(p -> parent == curproc){
        p -> parent = initproc;
      }
      p = p -> next;
    }
  }

  // Jump into the scheduler, never to return.
 //curproc->state = ZOMBIE;
  stateTransition(RUNNING, ZOMBIE, curproc);
  sched();
  panic("zombie exit");
}

#else
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

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}
#endif

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P4
int
wait(void)
{
  struct proc * p; 
  int havekids;
  uint pid;
  struct proc *curproc = myproc();


  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    p = ptable.list[ZOMBIE].head;
    while (p){
      if(p -> parent == curproc){
        havekids = 1;
        pid = p-> pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        stateTransition(ZOMBIE, UNUSED, p);
        release(&ptable.lock);
        return pid;
      }
      p = p-> next;
    }
 
    for(int i = 0; i <= MAXPRIO; i++){
      p = ptable.ready[i].head;
      while(p){
        if(p->parent == curproc)
          havekids = 1;
        p = p-> next;
      }
    }
   
    p = ptable.list[RUNNING].head;
    while(p){
      if(p->parent == curproc)
        havekids = 1;
      p = p-> next;
    }
    p = ptable.list[SLEEPING].head;
    while(p){
      if(p->parent == curproc)
        havekids = 1;
      p = p-> next;
    }
    p = ptable.list[EMBRYO].head;
    while(p){
      if(p->parent == curproc)
        havekids = 1;
      p = p-> next;
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

#elif CS333_P3
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(int i = 1; i <= ZOMBIE; ++i){

      p = ptable.list[i].head;
      while (p){
        if(p -> parent != curproc){
         p = p -> next;
         continue;
        }
        havekids = 1;
        if(p -> state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          stateTransition(ZOMBIE, UNUSED, p);
          release(&ptable.lock);
          return pid;
        }
        p = p-> next;
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

#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
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
        p->kstack = 0;
        freevm(p->pgdir);
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
#endif

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P4
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    if(ticks >= ptable.PromoteAtTime && MAXPRIO){
      promoteAll();
      ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
    }

    //p = ptable.list[RUNNABLE].head;
    for(int i = 0; i <= MAXPRIO; ++i){
      p = ptable.ready[i].head;
      if (p){
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
#ifdef PDX_XV6
        idle = 0;  // not idle this timeslice
#endif // PDX_XV6
        c->proc = p;
        switchuvm(p);
        listTransition(&ptable.ready[i], &ptable.list[RUNNING], RUNNABLE, RUNNING, p);//process begins running

        //Note that the transition from the RUNNING state can put the process
        //into one of three different states: RUNNABLE,SLEEPING, or ZOMBIE.
#ifdef CS333_P2
        p->cpu_ticks_in = ticks; //ticks when scheduled
#endif //CS333_P2
        swtch(&(c->scheduler), p->context);
        switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
        c->proc = 0;
      }
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#elif defined(CS333_P3)
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    p = ptable.list[RUNNABLE].head;
      if (p){
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
#ifdef PDX_XV6
        idle = 0;  // not idle this timeslice
#endif // PDX_XV6
        c->proc = p;
        switchuvm(p);
        stateTransition(RUNNABLE, RUNNING, p);  //process begins running
        //Note that the transition from the RUNNING state can put the process
        //into one of three different states: RUNNABLE,SLEEPING, or ZOMBIE.
#ifdef CS333_P2
        p->cpu_ticks_in = ticks; //ticks when scheduled
#endif //CS333_P2
        swtch(&(c->scheduler), p->context);
        switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}

#else //run original scheduler
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
    
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;  //process begins running
#ifdef CS333_P2
      p->cpu_ticks_in = ticks; //ticks when scheduled
#endif //CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif

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
  #ifdef CS333_P2
  myproc()->cpu_ticks_total += ticks - myproc()->cpu_ticks_in;
  #endif // CS333_P2
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);  //DOC: yieldlock
#ifdef CS333_P4
  curproc->budget -= (ticks - curproc->cpu_ticks_in);
  if(curproc -> budget <= 0 && curproc-> priority > 0){
    curproc -> priority -= 1;
    curproc -> budget = DEFAULT_BUDGET;
  }
  listTransition(&ptable.list[RUNNING], &ptable.ready[curproc-> priority], RUNNING, RUNNABLE, curproc);
#elif CS333_P3
  stateTransition(RUNNING, RUNNABLE, curproc);
#else
  curproc->state = RUNNABLE;
#endif
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
#ifdef CS333_P3
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;

//  p->state = SLEEPING;
#ifdef CS333_P4
  p->budget -= (ticks - p->cpu_ticks_in); //step 6. when a process is removed from the CPU, the budget is updatedaccording to this formula
  if(p->budget <= 0 && p->priority > 0){
    p -> priority -= 1;
    p -> budget = DEFAULT_BUDGET;
  }
#endif //CS333_P4
  stateTransition(RUNNING, SLEEPING, p);
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}



#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
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
    if (lk) acquire(lk);
  }
}
#endif

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P4
static void
wakeup1(void *chan)
{
  if(!holding(&ptable.lock))
    panic("lock not held in wakeup1");

  struct proc *p = ptable.list[SLEEPING].head;
  while (p){
    struct proc *t = p;
    p = p -> next;
    if (t -> chan == chan)
      listTransition(&ptable.list[SLEEPING], &ptable.ready[t->priority], SLEEPING, RUNNABLE, t);
  }
}

#elif CS333_P3
static void
wakeup1(void *chan)
{
  if(!holding(&ptable.lock))
    panic("lock not held in wakeup1");

  struct proc *p = ptable.list[SLEEPING].head;
  while (p){
    struct proc *t = p;
    p = p -> next;
    if (t -> chan == chan)
      stateTransition(SLEEPING, RUNNABLE, t);
  }
}

#else
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif

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
#ifdef CS333_P3
int
kill(int pid)  //Search runnable, sleep, zombie, embryo, and running lists for the pid to kill
{
  struct proc *p;

  acquire(&ptable.lock);
#ifdef CS333_P4
  for(int i = 0; i <= MAXPRIO; ++i){
    p = ptable.ready[i].head;
    while (p){
      if(p->pid == pid){
        p->killed = 1;
        release(&ptable.lock);
        return 0;
      }
      p = p-> next;
    }
  }
 
 p = ptable.list[SLEEPING].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;

      // Wake process from sleep if necessary.
      listTransition(&ptable.list[SLEEPING], &ptable.ready[p->priority], SLEEPING, RUNNABLE, p);
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }

#elif CS333_P3
  p = ptable.list[RUNNABLE].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }

  p = ptable.list[SLEEPING].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;

      // Wake process from sleep if necessary.
      stateTransition(SLEEPING, RUNNABLE, p);
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }
#endif

  p = ptable.list[ZOMBIE].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }
  
  p = ptable.list[EMBRYO].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }

  p = ptable.list[RUNNING].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p -> next;
  }
            
  //If a process with the pid is not found
  release(&ptable.lock);
  return -1;
}

#else
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
#endif

#ifdef CS333_P1
//Calculates and prints elapsed time, state, size of a process
void
procdumpP1(struct proc *p, char *state)
{
  int x = p->start_ticks;
  int y = ticks;
  int whole_ticks = (y-x)/1000;
  int rem_ticks = (y-x) % 1000;

  cprintf("%d\t%s\t%d.%d\t%s\t%d\t", p->pid, p->name, whole_ticks, rem_ticks, state, p->sz);

  return;
}
#endif
#ifdef CS333_P2
void
procdumpP2(struct proc *p, char *state)
{
  uint elapsed_dec = (ticks - p->start_ticks)%1000;
  char * e_fill = "";
  if (elapsed_dec < 10)
    e_fill = "00";
  if (elapsed_dec >= 10 && elapsed_dec < 100)
    e_fill = "0";

  uint cpu_dec = p->cpu_ticks_total%1000;
  char * cpu_fill = "";
  if(cpu_dec < 10)
    cpu_fill = "00";
  if(cpu_dec >= 10 && cpu_dec < 100)
    cpu_fill = "0";
  
  uint ppid;
  if(p->pid == 1)
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t%d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t",
  p->pid, p->name, p->uid, p->gid, ppid,
  (elapsed_dec)/1000, e_fill,
  elapsed_dec,
  cpu_dec/1000, cpu_fill,
  cpu_dec,
  state, p->sz);

}
#endif //CS333_P2
#ifdef CS333_P4
void
procdumpP3P4(struct proc *p, char *state)
{
  uint elapsed_dec = (ticks - p->start_ticks)%1000;
  char * e_fill = "";
  if (elapsed_dec < 10)
    e_fill = "00";
  if (elapsed_dec >= 10 && elapsed_dec < 100)
    e_fill = "0";

  uint cpu_dec = p->cpu_ticks_total%1000;
  char * cpu_fill = "";
  if(cpu_dec < 10)
    cpu_fill = "00";
  if(cpu_dec >= 10 && cpu_dec < 100)
    cpu_fill = "0";

  uint ppid;
  if(p->pid == 1)
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t%d\t%d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t",
  p->pid, p->name, p->uid, p->gid, ppid, p->priority,
  (elapsed_dec)/1000, e_fill,
  elapsed_dec,
  cpu_dec/1000, cpu_fill,
  cpu_dec,
  state, p->sz);

}
#endif //CS333_P4

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  #if defined(CS333_P4)
  #define HEADER "\nPID\tName UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
  #elif defined(CS333_P2)
  #define HEADER "\nPID\tName UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
  #elif defined(CS333_P1)
  #define HEADER "\nPID\tName Elapsed\tState\tSize\t PCs\n"
  #else
  #define HEADER "\n"
  #endif
  
  cprintf(HEADER);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state > UNUSED && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
  
  #if defined(CS333_P4)
    procdumpP3P4(p, state);
  #elif defined(CS333_P2)
    procdumpP2(p, state);
  #elif defined(CS333_P1)
    procdumpP1(p, state);
  #else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
  #endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

#ifdef CS333_P2
int 
setuid(int * uid)
{      
   myproc()->uid = * uid;
   return 0;
}

int
setgid(int * gid)
{
  myproc()->gid = * gid;
  return 0;
}

int 
getprocs(uint max, struct uproc* table){
  struct proc* p;
  int num = 0;

  //hint from mark in class - "acquire lock before accessing ptable data structure"
  acquire(&ptable.lock); //concurrency control
  for(p = ptable.proc; p < &ptable.proc[NPROC] && num < max; ++p){
//    if (num == max){
//      release(&ptable.lock);
 //     return num;
  //  }
    if (p->state != UNUSED && p->state != EMBRYO){ //only acquires active processes
      table[num].pid = p->pid;
      table[num].uid = p->uid;
      table[num].gid = p->gid;
      if(table[num].pid == 1) //"handles ppid of init"
        table[num].ppid = table[num].pid;
      else
        table[num].ppid = p->parent->pid;
#ifdef CS333_P4
      table[num].priority = p->priority; 
#endif //Cs333_P4
      table[num].elapsed_ticks = ticks - p->start_ticks; 
      table[num].CPU_total_ticks = p->cpu_ticks_total;
      strncpy(table[num].state, states[p->state], STRMAX);
      table[num].size = p->sz;
      strncpy(table[num].name, p->name, STRMAX);
      ++num;
      }
  }
  release(&ptable.lock);
  return num;
}
#endif //CS333_P2


#ifdef CS333_P3
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}

static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    cprintf("null");
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    cprintf("Process not found");
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}

//initialize lists
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#ifdef CS333_P4
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}

static void
initFreeList(void)
{
  if (!holding(&ptable.lock)){
    panic("lock not held\n");
  }

  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}

//Helper functions
static void
assertState(struct proc* p, enum procstate state){
  if (p->state != state)
    panic("Process is not in correct state");
}
#ifdef CS333_P4
static void
assertPriority(struct proc* p, int priority){
  if(p-> priority != priority)
    panic("Process not in correct priority");
}

#endif //CS333_P4
static void
stateTransition(enum procstate old, enum procstate new, struct proc* p){
  if(!p) return;
//  int lock = holding(&ptable.lock);
 // if(!lock){
 //   acquire(&ptable.lock);                          //1. Acquire ptable lock
 // }
  int rc = stateListRemove(&ptable.list[old], p); //2. Remove process from list
  if (rc == -1)                                     
    panic("State list removal failed");
  assertState(p, old);                  //3. Assert that the process was in the right state
  p->state = new;                       //4. Update the process
 // if (new == RUNNABLE)                        // If the new list is the ready list, place the process 
  //  stateListAdd(&ptable.ready[i], p);  
//  else                                           
  stateListAdd(&ptable.list[new], p);   //5. Put on new list
  assertState(p, new);                  // Assert process is in correct state
//  release(&ptable.lock);                //6. Release ptable lock
}

#ifdef CS333_P4
static void
listTransition(struct ptrs* old_list, struct ptrs* new_list, enum procstate old, enum procstate new, struct proc* p){
  int rc = stateListRemove(old_list, p);
  if (rc == -1)
    panic("State list removal failed");

  assertState(p, old);
  if(old == RUNNABLE)
    assertPriority(p, p-> priority);

  p->state = new;
  stateListAdd(new_list, p);
  assertState(p, new);
}
#endif //CS333_P4

void
printReadyList(){
  
  cprintf("Ready List Processes: \n");
  acquire(&ptable.lock);
  struct proc* p;
#ifdef CS333_P4
  for(int i =0; i <= MAXPRIO; ++i){
    p = ptable.ready[i].head;
    cprintf("MAXPRIO-%d: ", i);
    while (p){
      cprintf("(%d, %d)", p -> pid, p->budget);
      if(p == ptable.ready[i].tail)
       cprintf("\n");
      else
      cprintf(" -> ");
    p = p -> next;
    }  
  }

#elif CS333_P3
  p = ptable.list[RUNNABLE].head;
  
  while (p){
    cprintf("%d", p -> pid);
  if(p == ptable.list[RUNNABLE].tail)
      cprintf("\n");
    else
      cprintf(" -> ");
      
    p = p -> next;
  }
#endif
  release(&ptable.lock);
}

void
printFreeList(){
  acquire(&ptable.lock); 
  int n = 0;
  struct proc* p;
  p = ptable.list[UNUSED].head;
  while(p){
    n += 1;
    p = p -> next;
  }

  release(&ptable.lock);
  cprintf("Free List Size: %d processes\n", n);
}

void
printSleepList(){
   
  cprintf("Sleep List Processes: \n");
  acquire(&ptable.lock);
  struct proc* p;

  p = ptable.list[SLEEPING].head;
  
  while (p){
    cprintf("%d", p -> pid);
    if(p == ptable.list[SLEEPING].tail)
      cprintf("\n");
      else
        cprintf(" -> ");
      
    p = p -> next;
  }
 
  release(&ptable.lock);
}

void
printZombieList(){
  cprintf("Zombie List Processes: \n");
  acquire(&ptable.lock);
  struct proc* p;

  p = ptable.list[ZOMBIE].head;
  
  while (p){
    cprintf("(%d, %d)", p -> pid, p -> parent -> pid);
    if(p == ptable.list[ZOMBIE].tail)
      cprintf("\n");
      else
        cprintf(" -> ");

    p = p -> next;
  }
  release(&ptable.lock);
}

#endif //CS333_P3
#ifdef CS333_P4
int
setpriority(int pid, int priority)
{
  acquire(&ptable.lock);
  struct proc* p;
 
 for(int i = 0; i <= MAXPRIO; i++){
    p = ptable.ready[i].head;
    while(p){
      if(p->pid == pid){
        p->priority = priority;
        p->budget = DEFAULT_BUDGET;
        if(priority != i){
          listTransition(&ptable.ready[i], &ptable.ready[priority], RUNNABLE, RUNNABLE, p);
        }
        release(&ptable.lock);
        return 0;
      }
      p = p-> next;
    }
  }

  for(int i = 1; i <= ZOMBIE; ++i){
    p = ptable.list[i].head;
    if(p->state == RUNNABLE)
      continue;
    while (p){
      if(p->pid == pid){
        p->priority = priority;
        p->budget = DEFAULT_BUDGET;
        release(&ptable.lock);
        return 0;
      }
      p = p-> next;
    }
  }

  release(&ptable.lock);
  return -1;
}

int
getpriority(int pid)
{
  acquire(&ptable.lock);
  struct proc * p;

  for(int i = 0; i <= MAXPRIO; i++){
    p = ptable.ready[i].head;
    while(p){
      if(p->pid == pid){
        release(&ptable.lock);
        return(p->priority);
      }
      p = p-> next;
    }
  }
  for(int i = 1; i <=ZOMBIE; ++i){
    p = ptable.list[i].head;
    if(p->state == RUNNABLE)
      continue;
    while(p){
      if(p->pid == pid){
        release(&ptable.lock);
        return(p->priority);
       }
      p = p-> next;
    }
  }
  release(&ptable.lock);
  return -1;
}

void
promoteAll(void)
{ 
  //RUNNING
  struct proc * p = ptable.list[RUNNING].head;
  while(p){
    if(p->priority < MAXPRIO){
      p->priority += 1;
      p->budget = DEFAULT_BUDGET;
    }
    p = p->next;
  }
  
  //SLEEPING
  p = ptable.list[SLEEPING].head;
  while(p){
    if(p->priority < MAXPRIO){
      p->priority += 1;
      p->budget = DEFAULT_BUDGET;
    }
    p = p-> next;
  }
  //RUNNABLE
  for(int i = 1; i < MAXPRIO; ++i){
    p = ptable.ready[i].head;
    while(p){
      listTransition(&ptable.ready[i], &ptable.ready[i+1], RUNNABLE, RUNNABLE, p);
      p-> priority += 1;
      p-> budget = DEFAULT_BUDGET;
      p = p-> next;
    }
  }
}
#endif //CS333_P4









