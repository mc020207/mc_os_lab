#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>

int pipeAlloc(File** f0, File** f1) {
    // TODO
    Pipe* p;
    p=NULL;
    *f0=*f1=0;
    if ((*f0=filealloc())==0||(*f1=filealloc())==0) goto bad;
    if ((p=(Pipe*)kalloc(sizeof(Pipe)))==0) goto bad;
    p->readopen = 1;
    p->writeopen = 1;
    p->nwrite = 0;
    p->nread = 0;
    init_sem(&p->rlock, 0);
    init_sem(&p->wlock, 0);
    init_spinlock(&p->lock);
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = p;
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = p;
    return 0;
bad:
    if (p) kfree((char *)p);
    if (*f0) fileclose(*f0);
    if (*f1) fileclose(*f1);
    return -1;
}

void pipeClose(Pipe* pi, int writable) {
    // TODO
    _acquire_spinlock(&pi->lock);
    if (writable){
        pi->writeopen=0;
        post_sem(&pi->rlock);
    }else{
        pi->readopen=0;
        post_sem(&pi->wlock);
    }
    if (pi->readopen==0&&pi->writeopen==0){
        _release_spinlock(&pi->lock);
        kfree((void*) pi);
    }else{
        _release_spinlock(&pi->lock);
    }
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO
    _acquire_spinlock(&pi->lock);
    for (int i=0;i<n;i++){
        while (pi->nwrite==pi->nread+PIPESIZE){
            if (pi->readopen==0||thisproc()->killed){
                _release_spinlock(&pi->lock);
                return -1;
            }
            post_sem(&pi->rlock);
            _release_spinlock(&pi->lock);
            unalertable_wait_sem(&pi->wlock);
        }
        pi->data[pi->nwrite++ % PIPESIZE] = *((char *)addr + i);
    }
    post_sem(&pi->rlock);
    _release_spinlock(&pi->lock);
    return n;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO
    _acquire_spinlock(&pi->lock);
    while (pi->nread==pi->nwrite&&pi->writeopen){
        if (thisproc()->killed){
            _release_spinlock(&pi->lock);
            return -1;
        }
        _release_spinlock(&pi->lock);
        unalertable_wait_sem(&pi->rlock);
    }
    int i;
    for (i=0;i<n;i++){
        if (pi->nread==pi->nwrite) break;
        *((char *)addr + i) = pi->data[pi->nwrite++ % PIPESIZE];
    }
    post_sem(&pi->wlock);
    _release_spinlock(&pi->lock);
    return i;
}