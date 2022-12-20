#include <common/string.h>
#include <common/list.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

struct container root_container;
extern struct proc root_proc;

void activate_group(struct container* group);

void set_container_to_this(struct proc* proc)
{
    proc->container = thisproc()->container;
}

void init_container(struct container* container)
{
    memset(container, 0, sizeof(struct container));
    container->parent = NULL;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // TODO: initialize namespace (local pid allocator)
    init_pidmanager(&container->localpidmanager);
}
SpinLock containerlock;
struct container* create_container(void (*root_entry)(), u64 arg)
{
    // TODO
    _acquire_spinlock(&containerlock);
    struct container* ans=kalloc(sizeof(struct container));
    init_container(ans);
    ans->rootproc=kalloc(sizeof(struct proc));
    // printk("root_proc:%p\n",ans->rootproc);
    init_proc(ans->rootproc);
    set_parent_to_this(ans->rootproc);
    ans->rootproc->container=ans;
    ans->parent=thisproc()->container;
    start_proc(ans->rootproc,root_entry,arg);
    activate_group(ans);
    _release_spinlock(&containerlock);
    return ans;
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
    init_spinlock(&containerlock);
}
