#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock* sblock;
static const BlockDevice* device;

static SpinLock lock;     // protects block cache.
static ListNode head;     // the list of all allocated in-memory block.
static LogHeader header;  // in-memory copy of log header block.
static usize blocknum;
// hint: you may need some other variables. Just add them here.
struct LOG {
    /* data */
} log;
// read the content from disk.
static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&header);
}

// initialize a block struct.
static void init_block(Block* block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;
    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    _acquire_spinlock(&lock);
    usize ans=blocknum;
    _release_spinlock(&lock);
    return ans;
}

// see `cache.h`.
static Block* cache_acquire(usize block_no) {
    // TODO
    _acquire_spinlock(&lock);
    Block* ans=NULL;
    _for_in_list(p,&head){
        if (p==&head) break;
        Block* now = container_of(p,Block,node);
        if (now->block_no==block_no){
            ans=now;
            break;
        }
    }
    if (ans){
        wait_sem(&ans->lock);
        _detach_from_list(&ans->node);
        _insert_into_list(&head,&ans->node);
        ans->acquired=1;
        _release_spinlock(&lock);
        return ans;
    }
    // int blocknum=0;Block *del=NULL;
    // for (auto p=head.prev;p!=&head;p=p->prev){
    //     blocknum++;
    //     Block* now=container_of(p,Block,node);
    //     if (now->acquired==0){
    //         del=del!=NULL?del:now;
    //     }
    // }
    // if (blocknum>=EVICTION_THRESHOLD&&del!=NULL){
    //     if (del->pinned){
    //         device_write(del);
    //     }
    //     ans=del;
    //     _detach_from_list(&del->node);
    // }else{
    //     ans=kalloc(sizeof(Block));
    // }
    if (blocknum>=EVICTION_THRESHOLD){
        ListNode*p=head.prev,*q;
        while (1){
            if (p==&head||blocknum<EVICTION_THRESHOLD) break;
            q=p->prev;
            Block* now=container_of(p,Block,node);
            if (now->acquired==0){
                if (now->pinned){
                    device_write(now);
                }
                _detach_from_list(p);
                blocknum--;
                free(now);
            }
            p=q;
        }
    }
    ans=kalloc(sizeof(Block));
    blocknum++;
    init_block(ans);
    ans->block_no=block_no;
    ans->acquired=1;
    ans->valid=1;
    device_read(ans);
    _insert_into_list(&head,&ans->node);
    wait_sem(&ans->lock);
    _release_spinlock(&lock);
    return ans;
    return NULL;
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    block->acquired=0;
    post_sem(&block->lock);
}

// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;
    // TODO
    init_spinlock(&lock);
    init_list_node(&head);
    header.num_blocks=0;
    memset(header.block_no,0,LOG_MAX_SIZE);
    }

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if(ctx==NULL){
        device_write(block);
        block->pinned=false;
    }
}

// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO

}
BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};

