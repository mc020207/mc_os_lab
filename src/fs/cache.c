#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock* sblock;
static const BlockDevice* device;

static SpinLock lock;     // protects block cache.
static SpinLock loglock;
static SpinLock bitmaplock;
static ListNode head;     // the list of all allocated in-memory block.
static LogHeader header;  // in-memory copy of log header block.
static usize blocknum;
// hint: you may need some other variables. Just add them here.
static void cache_sync(OpContext* ctx, Block* block);
struct LOG {
    bool iscommit;
    int outstanding;
    Semaphore logsem;
} log;
// read the content from disk.
static void movetohead(ListNode* p){
    _detach_from_list(p);
    _insert_into_list(&head,p);
}

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
    init_sem(&block->lock,1);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    return blocknum;
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
        ans->acquired=1;
        _release_spinlock(&lock);
        bool f=wait_sem(&ans->lock);
        if (!f){
            PANIC();
        }
        _acquire_spinlock(&lock);
        movetohead(&ans->node);
        _release_spinlock(&lock);
        return ans;
    }
    if (blocknum>=EVICTION_THRESHOLD){
        ListNode*p=head.prev,*q;
        while (1){
            if (p==&head||blocknum<EVICTION_THRESHOLD) break;
            q=p->prev;
            Block* now=container_of(p,Block,node);
            if (!now->acquired&&!now->pinned){
                _detach_from_list(p);
                blocknum--;
                kfree(now);
            }
            p=q;
        }
    }
    ans=kalloc(sizeof(Block));
    init_block(ans);
    bool f=wait_sem(&ans->lock);
    if (!f){
        PANIC();
    }
    blocknum++;
    ans->block_no=block_no;
    ans->acquired=1;
    ans->valid=1;
    device_read(ans);
    _insert_into_list(&head,&ans->node);
    _release_spinlock(&lock);
    return ans;
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    _acquire_spinlock(&lock);
    block->acquired=0;
    post_sem(&block->lock);
    _release_spinlock(&lock);
}

static void copyblockdata(usize fromblockno,usize toblockno){
    Block *from=cache_acquire(fromblockno);
    Block *to=cache_acquire(toblockno);
    for (int j=0;j<BLOCK_SIZE;j++) to->data[j]=from->data[j];
    cache_sync(NULL,to);
    cache_release(from);
    cache_release(to);
}
// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;
    // TODO
    init_spinlock(&lock);
    init_spinlock(&loglock);
    init_spinlock(&bitmaplock);
    init_list_node(&head);
    blocknum=0;
    header.num_blocks=0;
    log.outstanding=log.iscommit=0;
    init_sem(&log.logsem,0);
    read_header();
    for (usize i=0;i<header.num_blocks;i++){
        copyblockdata(sblock->log_start+i+1,header.block_no[i]);
    }
    header.num_blocks=0;
    memset(header.block_no,0,LOG_MAX_SIZE);
    write_header();
}

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&loglock);
    ctx->rm=0;
    while (log.iscommit||header.num_blocks+(log.outstanding+1)*OP_MAX_NUM_BLOCKS>LOG_MAX_SIZE){
        _release_spinlock(&loglock);
        bool f=wait_sem(&log.logsem);
        if (!f){
            PANIC();
        }
        _acquire_spinlock(&loglock);
    }
    log.outstanding++;
    _release_spinlock(&loglock);
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if(ctx==NULL){
        device_write(block);
        return ;
    }
    _acquire_spinlock(&loglock);
    block->pinned=1;
    for (usize i=0;i<header.num_blocks;i++){
        if (block->block_no==header.block_no[i]){
            _release_spinlock(&loglock);
            return ;
        }
    }
    if (ctx->rm>=OP_MAX_NUM_BLOCKS||header.num_blocks>=LOG_MAX_SIZE){
        PANIC();
    }
    header.block_no[header.num_blocks++]=block->block_no;
    ctx->rm++;
    _release_spinlock(&loglock);
}

// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&loglock);
    if (log.iscommit) PANIC();
    log.outstanding--;
    if (log.outstanding>0){
        post_sem(&log.logsem);
        _release_spinlock(&loglock);
        return ;
    }
    log.iscommit=1;
    // printk("num_block:%d\n",header.num_blocks);
    for (int i=0;i<(int)header.num_blocks;i++){
        copyblockdata(header.block_no[i],sblock->log_start+i+1);
    }
    write_header();
    for (int i=0;i<(int)header.num_blocks;i++){
        Block *now=cache_acquire(header.block_no[i]);
        cache_sync(NULL,now);
        now->pinned=0;
        cache_release(now);
    }
    header.num_blocks=0;
    write_header();
    log.iscommit=0;
    post_sem(&log.logsem);
    _release_spinlock(&loglock);
    return (void)ctx;
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&bitmaplock);
    for (int blockstart=0;blockstart<(int)sblock->num_blocks;blockstart+=BLOCK_SIZE*8){
        Block* mp=cache_acquire(sblock->bitmap_start+blockstart/(BLOCK_SIZE*8));
        for (int add=0;add<BLOCK_SIZE&&blockstart+add*8<(int)sblock->num_blocks;add++){
            int temp=mp->data[add];
            for (int i=0;i<8&&blockstart+add*8+i<(int)sblock->num_blocks;i++){
                if ((temp&(1<<i))==0){
                    mp->data[add]|=(1<<i);
                    cache_sync(ctx,mp);
                    cache_release(mp);
                    Block* ans=cache_acquire(blockstart+add*8+i);
                    memset(ans->data,0,BLOCK_SIZE);
                    cache_sync(ctx,ans);
                    cache_release(ans);
                    _release_spinlock(&bitmaplock);
                    return blockstart+add*8+i;
                }
            }
        }
        cache_release(mp);
    }
    _release_spinlock(&bitmaplock);
    PANIC();
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO
    _acquire_spinlock(&bitmaplock);
    Block* mp=cache_acquire(sblock->bitmap_start+block_no/(8*BLOCK_SIZE));
    int idx=block_no%(8*BLOCK_SIZE);
    mp->data[idx/8]-=(1<<(idx%8));
    cache_sync(ctx,mp);
    cache_release(mp);
    _release_spinlock(&bitmaplock);
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

