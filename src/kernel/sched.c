#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>

extern bool panic_flag;
extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock rqlock;
static ListNode rq;
static struct timer sched_timer[NCPU];

define_early_init(rq){
    init_spinlock(&rqlock);
    init_list_node(&rq);
    for (int i=0;i<NCPU;i++){
        sched_timer[i].triggered=1;
        sched_timer[i].data=i;
        sched_timer[i].elapse=10;
        sched_timer[i].handler=&sched_timer_handler;
    }
}

define_init(sched){
    for (int i=0;i<NCPU;i++){
        struct proc* p=kalloc(sizeof(struct proc));
        // init_proc(p);
        p->idle=1;
        p->state=RUNNING;
        cpus[i].sched.thisproc=cpus[i].sched.idle=p;
    }
}

struct proc* thisproc()
{
    //TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo* p)
{
    //TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->rq);
}

void _acquire_sched_lock()
{
    //TODO: acquire the sched_lock if need
    _acquire_spinlock(&rqlock);
}

void _release_sched_lock()
{
    //TODO: release the sched_lock if need
    _release_spinlock(&rqlock);
}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc* p)
{
    //TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    _acquire_sched_lock();
    if (p->state==RUNNING||p->state==RUNNABLE||p->state==ZOMBIE){
        _release_sched_lock();
        return false;
    }
    if (p->state==SLEEPING||p->state==UNUSED){
        p->state=RUNNABLE; 
        _insert_into_list(&rq,&p->schinfo.rq);
    }
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    //TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto this=thisproc();
    if (this!=cpus[cpuid()].sched.idle&&(this->state==RUNNING||this->state==RUNNABLE)){
        _detach_from_list(&this->schinfo.rq);
    }
    this->state=new_state;
    if (this!=cpus[cpuid()].sched.idle&&(new_state==RUNNABLE||new_state==RUNNING)){
        _insert_into_list(rq.prev,&this->schinfo.rq);
    }
}   
extern bool panic_flag;
static struct proc* pick_next()
{
    //TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if (panic_flag) return cpus[cpuid()].sched.idle;
    _for_in_list(p,&rq){
        if (p==&rq) continue;
        auto proc=container_of(p,struct proc,schinfo.rq);
        if (proc->state==RUNNABLE) return proc;
    }
    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(struct proc* p)
{
    //TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    // reset_clock(1000);
    cpus[cpuid()].sched.thisproc=p;
    // if (p->idle) return ;
    if (!sched_timer[cpuid()].triggered){
        cancel_cpu_timer(&sched_timer[cpuid()]);
    }
    set_cpu_timer(&sched_timer[cpuid()]);
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if (this->killed&&new_state!=ZOMBIE){
        _release_sched_lock();
        return ;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

void sched_timer_handler(struct timer* timerr){
    timerr->data=0;
    _acquire_sched_lock();
    _sched(RUNNABLE);
}   