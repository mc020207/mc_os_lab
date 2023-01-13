#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/container.h>
#include <kernel/pid.h>
#include <file.h>
struct proc root_proc;
extern struct container root_container;

static PIDManager globalmanager;

void kernel_entry();
void proc_entry();

static SpinLock plock;
define_early_init(plock){
    init_spinlock(&plock);
    init_pidmanager(&globalmanager);
}

void init_pidmanager(PIDManager* manager){
    manager->max=0;
    init_list_node(&manager->freep.node);
}

void reuse_pid(PIDManager* manager,int pid){
    PIDNode* pidn=kalloc(sizeof(PIDNode));
    init_list_node(&pidn->node);
    pidn->pid=pid;
    _insert_into_list(&manager->freep.node,&pidn->node);
}

int getpid(PIDManager* manager){
    // printk("getpid:%d\n",manager->max);
    if (_empty_list(&manager->freep.node)){
        return ++manager->max; 
    }else{
        auto pidn=container_of(manager->freep.node.next,PIDNode,node);
        int pid=pidn->pid;
        _detach_from_list(&pidn->node);
        kfree(pidn);
        return pid;
    }
}

void set_parent_to_this(struct proc* proc)
{
    //TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&plock);
    ASSERT(proc->parent==NULL);
    proc->parent=thisproc();
    _insert_into_list(&thisproc()->children,&proc->ptnode);
    _release_spinlock(&plock);
}

NO_RETURN void exit(int code)
{
    //TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the rootproc of the container, and notify the it if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&plock);
    _acquire_sched_lock();
    auto this=thisproc();
    ASSERT(this != this->container->rootproc && !this->idle);
    ASSERT(this!=&root_proc);
    this->exitcode=code; 
    int times=0;
    _for_in_list(p,&this->children){
        if (p==&this->children) continue;
        auto childproc=container_of(p,struct proc,ptnode);
        ASSERT(childproc->parent==this);
        childproc->parent=&root_proc;
        if (childproc->state==ZOMBIE){
            times++;
        } 
    }
    if (!_empty_list(&this->children)){
        _merge_list(&this->container->rootproc->children,this->children.next);
        _detach_from_list(&this->children);
        _release_sched_lock();
        for (int i=0;i<times;i++) post_sem(&this->container->rootproc->childexit);
        _acquire_sched_lock();
    }
    free_pgdir(&this->pgdir);
    _release_sched_lock();
    post_sem(&thisproc()->parent->childexit); 
    _acquire_sched_lock();
    _release_spinlock(&plock);
    _sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode, int* pid)
{
    //TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its local pid and exitcode
    // NOTE: be careful of concurrency
    auto this=thisproc();
    _acquire_spinlock(&plock);
    if (this->children.next==&this->children){
        _release_spinlock(&plock);
        return -1;
    }
    _release_spinlock(&plock);
    if (!wait_sem(&this->childexit)) return -1;
    _acquire_spinlock(&plock);
    _acquire_sched_lock();
    struct proc* zombienode=NULL;
    _for_in_list(p,&this->children){
        if (p==&this->children) continue;
        auto childproc=container_of(p,struct proc,ptnode);
        ASSERT(childproc->parent==this);
        ASSERT(&childproc->ptnode==p);
        if (childproc->state==ZOMBIE){
            zombienode=childproc;
            break;
        }
    }
    if (zombienode!=NULL){
        ASSERT(zombienode->state==ZOMBIE);
        _detach_from_list(&zombienode->ptnode);
        _detach_from_list(&zombienode->schinfo.rq);
        *exitcode=zombienode->exitcode;
        kfree_page(zombienode->kstack);
        *pid=zombienode->pid;
        int returnid=zombienode->localpid;
        reuse_pid(&globalmanager,zombienode->pid);
        reuse_pid(&zombienode->container->localpidmanager,zombienode->localpid);
        kfree(zombienode);
        _release_sched_lock();
        _release_spinlock(&plock);
        return returnid;
    }
    _release_sched_lock();
    _release_spinlock(&plock);
    return -1;
}

struct proc* find_proc(int pid,struct proc* now){
    if (now->pid==pid&&!is_unused(now)){
        now->killed=1;
        return now;
    }
    _for_in_list(p,&now->children){
        if (p==&now->children) continue;
        auto childproc=container_of(p,struct proc,ptnode);
        struct proc* temp=find_proc(pid,childproc);
        if(temp!=NULL) return temp;
    }
    return NULL;
}

int kill(int pid)
{
    //TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    _acquire_spinlock(&plock);
    struct proc* killproc = find_proc(pid,&root_proc);
    _release_spinlock(&plock);
    if (killproc!=NULL){
        if (((killproc->ucontext->elr)>>48)==0){
            alert_proc(killproc);
            return 0;
        }
        else return -1;
    }
    return -1;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    //TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its local pid
    // NOTE: be careful of concurrency
    _acquire_spinlock(&plock);
    if (p->parent==NULL){
        p->parent=&root_proc;
        _insert_into_list(&root_proc.children,&p->ptnode);
    }
    p->localpid=getpid(&p->container->localpidmanager);
    _release_spinlock(&plock);
    p->kcontext->lr=(u64)&proc_entry;
    p->kcontext->x0=(u64)entry;
    p->kcontext->x1=(u64)arg;
    // printk("start_proc: %d\n",p->localpid);
    int id=p->localpid; // why?
    activate_proc(p);
    return id;
}

void init_proc(struct proc* p)
{
    //TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    _acquire_spinlock(&plock);
    memset(p,0,sizeof(*p));
    p->pid=getpid(&globalmanager);
    p->container=&root_container;
    p->killed=0;
    p->idle=0;
    init_sem(&p->childexit,0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_pgdir(&p->pgdir);
    p->parent=NULL;
    p->kstack=kalloc_page();
    init_schinfo(&p->schinfo,0);
    p->kcontext=(KernelContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(KernelContext)-sizeof(UserContext));
    p->ucontext=(UserContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(UserContext));
    init_oftable(&p->oftable);
    _release_spinlock(&plock);
}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    /* TODO: Your code here. */
}