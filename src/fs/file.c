/* File descriptors */

#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>
#include <fs/inode.h>
#include <fs/cache.h>
#include "fs.h"

static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process
    for (int i=0;i<NOFILE;i++) oftable->openfile[i]=NULL;
}

/* Allocate a file structure. */
struct file* filealloc() {
    /* TODO: Lab10 Shell */
    File* ans;
    _acquire_spinlock(&ftable.lock);
    for (int i=0;i<NFILE;i++){
        if (ftable.filelist[i].ref==0){
            ftable.filelist[i].ref=1;
            _release_spinlock(&ftable.lock);
            return &(ftable.filelist[i]);
        }
    }
    _release_spinlock(&ftable.lock);
    return NULL;
}

/* Increment ref count for file f. */
struct file* filedup(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    ASSERT(f->ref>=1);
    f->ref+=1;
    _release_spinlock(&ftable.lock);
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    ASSERT(f->ref>=1);
    f->ref--;
    if(f->ref>0){
        _release_spinlock(&ftable.lock);
        return ;
    }
    struct file now=*f;
    f->type=FD_NONE;
    _release_spinlock(&ftable.lock);
    if (now.type==FD_PIPE){
        pipeClose(now.pipe,now.writable);
    }else if (now.type==FD_INODE){
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx,now.ip);
        bcache.end_op(&ctx);
    }
}

/* Get metadata about file f. */
int filestat(struct file* f, struct stat* st) {
    /* TODO: Lab10 Shell */
    if (f->type==FD_INODE){
        inodes.lock(f->ip);
        stati(f->ip,st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize fileread(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if (f->readable==0) return -1;
    if (f->type==FD_PIPE) return pipeRead(f->pipe,addr,n);
    if (f->type==FD_INODE){
        isize ans=0;
        inodes.lock(f->ip);
        ans=inodes.read(f->ip,addr,f->off,n);
        if (ans>0) f->off+=ans;
        inodes.unlock(f->ip);
        return ans;
    }
    PANIC();
    return 0;
}

/* Write to file f. */
isize filewrite(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if (f->writable==0) return -1;
    if (f->type==FD_PIPE) return pipeWrite(f->pipe,addr,n);
    if (f->type==FD_INODE){
        isize maxbytes=((OP_MAX_NUM_BLOCKS-4)/2)*BLOCK_SIZE;
        // 2 blocks for each write block
        // 1 block for inode
        // 1 block for map
        // 2 blocks for IndirectBlock
        isize idx=0;
        while (idx<n){
            isize len=MIN(n-idx,maxbytes);
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            isize reallen=inodes.write(&ctx,f->ip,addr+idx,f->off,len);
            // ASSERT(reallen==len);
            if (reallen>0) f->off+=reallen;
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
            if (reallen<0) break;
            ASSERT(reallen==len);
            idx+=reallen;
        }
        if (idx==n) return n;
        return -1;
    }
    return 0;
}
