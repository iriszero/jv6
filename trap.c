#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  SETGATE(idt[T_MY_SYSCALL], 1, SEG_KCODE<<3, vectors[T_MY_SYSCALL], DPL_USER);

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
    if(proc->process->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->process->killed)
      exit();
    return;
  }
  else if(tf->trapno == T_MY_SYSCALL){
    if(proc->process->killed)
      exit();
    proc->tf = tf;
    interrupt_128();
    if(proc->process->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpunum() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
			//if (ticks % BOOSTUP_PERIOD == 0) {
			//	boost_up();
			//}
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
            cpunum(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
		//if (proc->process->killed || proc->kstack == 0 || proc->state == UNUSED)
		//	return;
    if((proc == 0 || (tf->cs&3) == 0)){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x pid %d tid %d (cr2=0x%x) kstack=%x\n state %d",
              tf->trapno, cpunum(), tf->eip, proc->pid, proc->tid, rcr2(), proc->kstack, proc->state);
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d tid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            proc->pid, proc->tid, 
						proc->name, tf->trapno, tf->err, cpunum(), tf->eip,
            rcr2());
    proc->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(proc && proc->process->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
		yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->process->killed && (tf->cs&3) == DPL_USER)
    exit();
}
