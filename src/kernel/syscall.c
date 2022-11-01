#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    //TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->x[8], ret = 0;
    if (id < NR_SYSCALL){
        ret=((u64(*)(u64,u64,u64,u64,u64,u64))syscall_table[id])(context->x[0],context->x[1],context->x[2],context->x[3],context->x[4],context->x[5]);
        context->x[0]=ret;
    }
}
