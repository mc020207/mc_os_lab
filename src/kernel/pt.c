#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <driver/memlayout.h>
static void freePTEntry(PTEntriesPtr p,int deep);
extern struct page refpage[PHYSTOP/PAGE_SIZE];
static void* kkalloc(){
    void *new_page=kalloc_page();
    memset(new_page,0,PAGE_SIZE);
    return new_page;
}

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    //TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    PTEntriesPtr p0=pgdir->pt,p1,p2,p3;
    if (p0==NULL){
        if (alloc==0) return NULL;
        p0=kalloc_page();
        memset(p0,0,PAGE_SIZE);
        pgdir->pt=p0;
    }
    if (!(p0[VA_PART0(va)]&PTE_VALID)){
        if (alloc==0) return NULL;
        p0[VA_PART0(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p1=(PTEntriesPtr)P2K(PTE_ADDRESS(p0[VA_PART0(va)]));
    if (!(p1[VA_PART1(va)]&PTE_VALID)){
        if (alloc==0) return NULL;
        p1[VA_PART1(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p2=(PTEntriesPtr)P2K(PTE_ADDRESS(p1[VA_PART1(va)]));
    if (!(p2[VA_PART2(va)]&PTE_VALID)){
        if (alloc==0) return NULL;
        p2[VA_PART2(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p3=(PTEntriesPtr)P2K(PTE_ADDRESS(p2[VA_PART2(va)]));
    return &p3[VA_PART3(va)];
}

void init_pgdir(struct pgdir* pgdir)
{
/*     struct pgdir{
        PTEntriesPtr pt;
        SpinLock lock; 
        ListNode section_head;
        bool online;
    }; */
    pgdir->pt = kalloc_page();
    memset(pgdir->pt,0,PAGE_SIZE);
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
    pgdir->online=0;
    init_sections(&pgdir->section_head);
}

void free_pgdir(struct pgdir* pgdir)
{
    //TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if (!pgdir->pt) return ;
    freePTEntry(pgdir->pt,0);
    pgdir->pt=NULL;
    while (pgdir->section_head.next!=&pgdir->section_head){
        struct section* now=container_of(pgdir->section_head.next,struct section,stnode);
        _detach_from_list(pgdir->section_head.next);
        kfree(now);
    }
}
static void freePTEntry(PTEntriesPtr p,int deep){
    if (deep==3||p==NULL){
        kfree_page(p);
        return ;
    }
    for (int i=0;i<N_PTE_PER_TABLE;i++){
        if (p[i]!=NULL) {
            freePTEntry((PTEntriesPtr)P2K(PTE_ADDRESS(p[i])),deep+1);
            p[i]=NULL;
        }
    }
    kfree_page(p);
}
void  attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags){
    auto pte = get_pte(pd, va, true);
    *pte = K2P(ka) | flags;
    _increment_rc(&refpage[K2P(ka) / PAGE_SIZE].ref);
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
}


