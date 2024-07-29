#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "traps.h"
#include "spinlock.h"

// Function prototypes
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

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
    if(tf->trapno == T_PGFLT) {
        uint addr = rcr2(); // Get the faulting address
        struct proc *p = myproc();
        pte_t *pte;
        char *mem;

        if (addr >= p->sz) {
            cprintf("Segmentation Fault\n");
            p->killed = 1;
        } else if (addr >= MAPBASE && addr < KERNBASE) {
            // Handle demand paging
            if ((pte = walkpgdir(p->pgdir, (void *)addr, 0)) == 0) {
                cprintf("Page Fault: invalid address\n");
                p->killed = 1;
            } else if (!(*pte & PTE_P)) {
                mem = kalloc();
                if (!mem) {
                    cprintf("Page Fault: out of memory\n");
                    p->killed = 1;
                } else {
                    memset(mem, 0, PGSIZE);
                    if (mappages(p->pgdir, (void *)PGROUNDDOWN(addr), PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
                        kfree(mem);
                        cprintf("Page Fault: mapping failed\n");
                        p->killed = 1;
                    } else {
                        // Update the mmap info
                        for (int i = 0; i < p->n_mmaps; i++) {
                            if (addr >= p->mmaps[i].addr && addr < p->mmaps[i].addr + p->mmaps[i].length) {
                                p->mmaps[i].n_loaded_pages++;
                                break;
                            }
                        }
                    }
                }
            }
        } else {
            cprintf("Segmentation Fault\n");
            p->killed = 1;
        }
        return;
    }
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
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
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
