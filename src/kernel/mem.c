#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <common/string.h>
#include <fs/cache.h>
RefCount alloc_page_cnt;
SpinLock memlock;
struct page refpage[PHYSTOP/PAGE_SIZE];
void *zeor_page=0;
int pagenum=0;
define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    init_spinlock(&memlock);
    // for (int i=0;i<PHYSTOP/PAGE_SIZE;i++) init_rc(&refpage[i].ref);
}

// All usable pages are added to the queue.
// NOTE: You can use the page itself to store allocator data of it.
// In this example, the fix-lengthed meta-data of the allocator are stored in .bss (static QueueNode* pages),
//  and the per-page allocator data are stored in the first sizeof(QueueNode) bytes of pages themselves.
//
// See API Reference for more information on given data structures.
static QueueNode* pages;
extern char end[];
typedef struct node{
    struct node *next;
    int size;
    int free;
} node;
node *free[4];
define_early_init(pages)
{
    for (u64 p = PAGE_BASE((u64)&end) + 2 * PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE){
	   add_to_queue(&pages, (QueueNode*)p);
        pagenum++;
    }
    zeor_page=(void *)(PAGE_BASE((u64)&end) + PAGE_SIZE);
    memset(zeor_page,0,PAGE_SIZE);
    refpage[K2P(zeor_page)/PAGE_SIZE].ref.count=1;
}

// Allocate: fetch a page from the queue of usable pages.
void* kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    void*page=fetch_from_queue(&pages);
    _increment_rc(&refpage[K2P(page)/PAGE_SIZE].ref);
    return page;
}
void merge(node *h){
    // h->free=1;
    node*p=h->next;
    if (p==NULL) return ;
    if (p->free==1&&PAGE_BASE((u64)h)==PAGE_BASE((u64)p)){
        h->next=p->next;
        h->size+=p->size+sizeof(node);
    }
    h->free=1;
}
// Free: add the page to the queue of usable pages.
void kfree_page(void* p)
{
    _decrement_rc(&refpage[K2P(p) / PAGE_SIZE].ref);
    if (refpage[K2P(p)/PAGE_SIZE].ref.count==0){
        _decrement_rc(&alloc_page_cnt);
        add_to_queue(&pages, (QueueNode*)p);
    }
}
void* kalloc(isize size){
    _acquire_spinlock(&memlock);
    size=(size/8+(size%8!=0))*8;
    node *h=free[cpuid()];
    while (h){
        if (h->free==1) {
            merge(h);
            if(h->size>=size) break;
        }
        h=h->next;
    }
    if (h==NULL){
        node *p=(node*)kalloc_page();
        p->next=free[cpuid()];
        p->size=PAGE_SIZE-sizeof(node);
        p->free=1;
        free[cpuid()]=p;
        h=p;
    }
    if (h->size-(u64)size>sizeof(node)){
        node *p=(node*)((u64)h+sizeof(node)+size);
        p->free=1;
        p->size=h->size-(u64)size-sizeof(node);
        p->next=h->next;
        h->next=p;
    }
    h->size=size;
    h->free=0;
    _release_spinlock(&memlock);
    return (void*)((u64)h+sizeof(node));
}

void kfree(void* p){
    _acquire_spinlock(&memlock);
    node *h=(node*)((u64)p-sizeof(node));
    merge(h);
    _release_spinlock(&memlock);
}

u64 left_page_cnt(){
    return pagenum-alloc_page_cnt.count;
}
WARN_RESULT void* get_zero_page(){
    return zeor_page;
}
bool check_zero_page(){
    printk("%lld\n", refpage[K2P(zeor_page) / PAGE_SIZE].ref.count);
    int *temp=(int *)zeor_page;
    for (u64 i=0;i<PAGE_SIZE/sizeof(int);i++){
        if (temp[i]) return false;
    }
    return true;
}
u32 write_page_to_disk(void* ka){
    u32 bno=find_and_set_8_blocks();
    for (int i=0;i<8;i++) block_device.write(bno+i,(u8*)ka+i*512);
    return bno;
}
void read_page_from_disk(void* ka, u32 bno){
    for (int i=0;i<8;i++) block_device.read(bno+i,(u8*)ka+i*512);
}