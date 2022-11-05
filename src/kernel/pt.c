#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/printk.h>
static void freePTEntry(PTEntriesPtr p,int deep);

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
    if (p0[VA_PART0(va)]==NULL||!PTE_VALID){
        if (alloc==0) return NULL;
        p0[VA_PART0(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p1=(PTEntriesPtr)P2K(PTE_ADDRESS(p0[VA_PART0(va)]));
    if (p1[VA_PART1(va)]==NULL||!PTE_VALID){
        if (alloc==0) return NULL;
        p1[VA_PART1(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p2=(PTEntriesPtr)P2K(PTE_ADDRESS(p1[VA_PART1(va)]));
    if (p2[VA_PART2(va)]==NULL||!PTE_VALID){
        if (alloc==0) return NULL;
        p2[VA_PART2(va)]=K2P(kkalloc())|PTE_TABLE;
    }
    p3=(PTEntriesPtr)P2K(PTE_ADDRESS(p2[VA_PART2(va)]));
    return &p3[VA_PART3(va)];
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir* pgdir)
{
    //TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if (!pgdir->pt) return ;
    freePTEntry(pgdir->pt,0);
    pgdir->pt=NULL;
    // PTEntriesPtr pt0=pgdir->pt;
    // if (!pt0) return ;
    // for (int i=0;i<N_PTE_PER_TABLE;i++){
    //     if(pt0[i]&PTE_VALID){
    //         PTEntriesPtr pt1=(PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
    //         for (int j=0;j<N_PTE_PER_TABLE;j++){
    //             if(pt1[j]&PTE_VALID){
    //                 PTEntriesPtr pt2=(PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
    //                 for (int k=0;k<N_PTE_PER_TABLE;k++){
    //                     if(pt2[k]&PTE_VALID){
    //                         PTEntriesPtr pt3=(PTEntriesPtr)P2K(PTE_ADDRESS(pt2[k]));
    //                         kfree_page(pt3);
    //                     }
    //                 }
    //                 kfree_page(pt2);
    //             }
    //         }
    //         kfree_page(pt1);
    //     }
    // }
    // kfree_page(pt0);
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



