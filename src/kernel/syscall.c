#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    //TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->x[8], ret = 0;
    // printk("id:%d\n",(int)id); 
    if (id < NR_SYSCALL&&syscall_table[id]!=NULL){
        // if (id==63){
        //     printk("******56******\n");
        // }
        ret=((u64(*)(u64,u64,u64,u64,u64,u64))syscall_table[id])(context->x[0],context->x[1],context->x[2],context->x[3],context->x[4],context->x[5]);
        context->x[0]=ret;
        // printk("in syscall proc%p context:%p elr:%p returnv:%lld\n",thisproc(),context,(void*)context->elr,ret);
    }
}

// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    // TODO
    start=start;
    size=size;
    return true;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    // TODO
    bool ans=1;
    for (u64 i=(u64)start;i<(u64)start+size;i=(i/BLOCK_SIZE+1)*BLOCK_SIZE){
        auto pte=get_pte(&thisproc()->pgdir,i,false);
        if (pte==NULL||((*pte)&PTE_RO)){
            ans=0;
            break;
        }
    }
    return ans;
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
