#include <common/string.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <sys/stat.h>

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;

static const SuperBlock* sblock;
static const BlockCache* cache;
// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);
    // TODO
    for (usize ino=1;ino<sblock->num_inodes;ino++){
        usize bno=to_block_no(ino);
        Block* now_block=cache->acquire(bno);
        InodeEntry* entry=get_entry(now_block,ino);
        if (entry->type==INODE_INVALID){
            memset(entry,0,sizeof(InodeEntry));
            entry->type=type;
            cache->sync(ctx,now_block);
            cache->release(now_block);
            return ino;
        }
        cache->release(now_block);
    }
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    _post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    if (inode->valid&&do_write){
        usize blockno=to_block_no(inode->inode_no);
        Block* now_block=cache->acquire(blockno);
        memcpy(get_entry(now_block,inode->inode_no),&inode->entry,sizeof(InodeEntry));
        cache->sync(ctx,now_block);
        cache->release(now_block);
    }
    if (!inode->valid){
        usize blockno=to_block_no(inode->inode_no);
        Block* now_block=cache->acquire(blockno);
        memcpy(&inode->entry,get_entry(now_block,inode->inode_no),sizeof(InodeEntry));
        inode->valid=1;
        cache->release(now_block);
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO
    _for_in_list(p,&head){
        if (p==&head) continue;
        auto now_inode=container_of(p,Inode,node);
        if (now_inode->inode_no==inode_no){
            _increment_rc(&now_inode->rc);
            inode_lock(now_inode);
            inode_sync(NULL,now_inode,0);
            inode_unlock(now_inode);
            _release_spinlock(&lock);
            return now_inode;
        }
    }
    Inode* new_inode=kalloc(sizeof(Inode));
    init_inode(new_inode);
    new_inode->inode_no=inode_no;
    _increment_rc(&new_inode->rc);
    inode_lock(new_inode);
    inode_sync(NULL,new_inode,0);
    inode_unlock(new_inode);
    _insert_into_list(&head,&new_inode->node);
    _release_spinlock(&lock);
    // PANIC();
    return new_inode;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    if (inode->entry.indirect!=0){
        Block* inblock=cache->acquire(inode->entry.indirect);
        u32* addrs=get_addrs(inblock);
        for (usize i=0;i<INODE_NUM_INDIRECT;i++){
            if (addrs[i]) cache->free(ctx,addrs[i]);
        }
        cache->release(inblock);
        cache->free(ctx,inode->entry.indirect);
        inode->entry.indirect=0;
    }
    for (int i=0;i<INODE_NUM_DIRECT;i++){
        if (inode->entry.addrs[i]){
            cache->free(ctx,inode->entry.addrs[i]);
            inode->entry.addrs[i]=0;
        }
    }
    inode->entry.num_bytes=0;
    inode_sync(ctx,inode,1);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    _increment_rc(&inode->rc);
    // TODO
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    unalertable_wait_sem(&inode->lock);
    _decrement_rc(&inode->rc);
    if (inode->rc.count==0&&inode->entry.num_links==0){
        inode->entry.type=INODE_INVALID;
        inode_clear(ctx,inode);
        inode_sync(ctx,inode,1);
        _acquire_spinlock(&lock);
        _detach_from_list(&inode->node);
        _release_spinlock(&lock);
        post_sem(&inode->lock);
        kfree(inode);
        return ;
    }
    post_sem(&inode->lock);
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    int numblock=offset;
    if (numblock<INODE_NUM_DIRECT){
        if (inode->entry.addrs[numblock]==0){
            *modified=1;
            inode->entry.addrs[numblock]=cache->alloc(ctx);
            // attention
            inode_sync(ctx,inode,1);
        }
        return inode->entry.addrs[numblock];
    }
    numblock-=INODE_NUM_DIRECT;
    if (inode->entry.indirect==0){
        inode->entry.indirect=cache->alloc(ctx);
        inode_sync(ctx,inode,1);
    }
    Block* inblock=cache->acquire(inode->entry.indirect);
    u32* addrs=get_addrs(inblock);
    if (addrs[numblock]==0){
        addrs[numblock]=cache->alloc(ctx);
        cache->sync(ctx,inblock);
        *modified=1;
    }
    usize ans=addrs[numblock];
    cache->release(inblock);
    return ans;
}

static int memcmp2(const char *s1,const char *s2){
    return memcmp(s1,s2,MAX(strlen(s1),strlen(s2)));
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        assert(inode->entry.major == 1);
        return console_read(inode, dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);
    for(usize i=offset;i<end;i=(i/BLOCK_SIZE+1)*BLOCK_SIZE){
        bool useless=0;
        usize bno=inode_map(NULL,inode,i/BLOCK_SIZE,&useless);
        Block* now_block=cache->acquire(bno);
        usize len=MIN(BLOCK_SIZE-i%BLOCK_SIZE,end-i);
        memcpy(dest,now_block->data+i%BLOCK_SIZE,len);
        dest+=len;
        cache->release(now_block);
    }
    // TODO
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    if (inode->entry.type == INODE_DEVICE) {
        assert(inode->entry.major == 1);
        return console_write(inode, src, count);
    }
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);
    if(entry->num_bytes<end){
        entry->num_bytes=end;
        inode_sync(ctx,inode,1);
    }
    for(usize i=offset;i<end;i=(i/BLOCK_SIZE+1)*BLOCK_SIZE){
        bool useless=0;
        usize bno=inode_map(ctx,inode,i/BLOCK_SIZE,&useless);
        Block* now_block=cache->acquire(bno);
        usize len=MIN(BLOCK_SIZE-i%BLOCK_SIZE,end-i);
        memcpy(now_block->data+i%BLOCK_SIZE,src,len);
        cache->sync(ctx,now_block);
        cache->release(now_block);
        src+=len;
    }
    // TODO
    return count;
}

// see `inode.h`.
// if can't get the file , index will be one free block's number
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    // TODO
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry now;
    // if(index) *index=INODE_MAX_BYTES;
    for (u32 i=0;i<entry->num_bytes;i+=sizeof(DirEntry)){
        inode_read(inode,(u8*)&now,i,sizeof(DirEntry));
        // if (index&&now.inode_no==0&&*index==INODE_MAX_BYTES) *index=i;
        if (now.inode_no&&memcmp2(name,now.name)==0){
            if (index) *index=i;
            return now.inode_no;
        }
    }
    // if (index&&*index==INODE_MAX_BYTES) *index=entry->num_bytes;
    return 0;
}

static usize inode_lookup2(Inode* inode, const char* name, usize* index) {
    // TODO
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry now;
    if(index) *index=INODE_MAX_BYTES;
    for (u32 i=0;i<entry->num_bytes;i+=sizeof(DirEntry)){
        inode_read(inode,(u8*)&now,i,sizeof(DirEntry));
        if (index&&now.inode_no==0&&*index==INODE_MAX_BYTES) *index=i;
        if (now.inode_no&&memcmp2(name,now.name)==0){
            if (index) *index=i;
            return now.inode_no;
        }
    }
    if (index&&*index==INODE_MAX_BYTES) *index=entry->num_bytes;
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    usize offset=0;
    if(inode_lookup2(inode,name,&offset)) return -1;
    DirEntry now;
    strncpy(now.name,name,FILE_NAME_MAX_LENGTH);
    now.inode_no=inode_no;
    inode_write(ctx,inode,(u8*)&now,offset,sizeof(DirEntry));
    return offset;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry now;
    inode_read(inode,(u8*)&now,index,sizeof(DirEntry));
    now.inode_no=0;
    inode_write(ctx,inode,(u8*)&now,index,sizeof(DirEntry));
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
/* Paths. */

/* Copy the next path element from path into name.
 *
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/* Look up and return the inode for a path name.
 *
 * If parent != 0, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Must be called inside a transaction since it calls iput().
 */
static Inode* namex(const char* path,
                    int nameiparent,
                    char* name,
                    OpContext* ctx) {
    /* TODO: Lab10 Shell */
    return 0;
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, 0, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, 1, name, ctx);
}

/*
 * Copy stat information from inode.
 * Caller must hold ip->lock.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}
