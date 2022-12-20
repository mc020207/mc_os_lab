#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h> 
#include <kernel/paging.h>
#include <common/defines.h>
#include <kernel/pt.h>
#include <common/sem.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/proc.h>
#define MASK (-(1<<12))
#define CLEAN(addr) (addr&MASK)
define_rest_init(paging){
    init_block_device();
	//TODO init		
}

void init_sections(ListNode *section_head){
    auto section_p = (struct section *)kalloc(sizeof(struct section));
    _insert_into_list(section_head, &section_p->stnode);
    init_sleeplock(&section_p->sleeplock);
    section_p->begin = 0;
    section_p->end = 0;
    section_p->flags = (0 | ST_HEAP);
}

u64 sbrk(i64 size){
	//TODO
	auto nowproc=thisproc();
	auto pd=&nowproc->pgdir;
	auto sec=container_of(pd->section_head.next,struct section,stnode);
	u64 ans=sec->end;
	sec->end+=size*PAGE_SIZE;
	if (size<0){
		for (i64 i=0;i<-size;i++){
			auto pte=get_pte(pd,sec->end+i*PAGE_SIZE,false);
			if (pte&&*pte){
                kfree_page((void *)(P2K(CLEAN(*pte))));
			    *pte=NULL;
            }
		}
	}
	attach_pgdir(pd);
    arch_tlbi_vmalle1is();
	return ans;
}	


void* alloc_page_for_user(){
	while(left_page_cnt() <= REVERSED_PAGES){
		//TODO
		return NULL;
	}
	return kalloc_page();
}

// caller must have the pd->lock
void swapout(struct pgdir *pd, struct section *st){
    st->flags|=ST_SWAP;
    for (u64 i=st->begin;i<st->end;i+=PAGE_SIZE){
        auto pte=get_pte(pd,i,false);
        if (pte&&(*pte)) *pte&=(~PTE_VALID);
    }
    u64 begin=st->begin;
    u64 end=st->end;
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    unalertable_wait_sem(&st->sleeplock);
    _release_spinlock(&pd->lock);
    if (!(st->flags&ST_FILE)){
        for (u64 i=begin;i<end;i+=PAGE_SIZE){
            auto pte=get_pte(pd,i,false);
            if (pte&&(!(*pte&PTE_VALID))){
                *pte=write_page_to_disk((void *)P2K(CLEAN(*pte)));
            }
        }
    }
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    sbrk(-(end-begin)/PAGE_SIZE);
    st->end=end;
    _post_sem(&st->sleeplock);
}
// Free 8 continuous disk blocks
void swapin(struct pgdir *pd, struct section *st){
    ASSERT(st->flags & ST_SWAP);
    // TODO
    unalertable_wait_sem(&st->sleeplock);
    u64 begin=st->begin,end=st->end;
    for (u64 i=begin;i<end;i+=PAGE_SIZE){
        auto pte=get_pte(pd,i,false);
        if (pte&&(*pte)){
            u32 bno=(*pte);
            void *newpage=alloc_page_for_user();
            read_page_from_disk(newpage,(u32)bno);
            *pte=K2P(newpage)|PTE_USER_DATA;
            release_8_blocks(bno);
        }
    }
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    st->flags &= ~ST_SWAP;
    _post_sem(&st->sleeplock);
}

int pgfault(u64 iss)
{
    auto p = thisproc();
    auto pd = &p->pgdir;
    u64 addr = arch_get_far();
    struct section *sec = NULL;
    _for_in_list(p,&pd->section_head){
        if (p==&pd->section_head) continue;
        sec=container_of(p,struct section,stnode);
        if (addr>=sec->begin) break;
    }
    auto pte = get_pte(pd,addr,true);
    if (*pte == NULL){
        if (sec->flags&ST_SWAP) swapin(pd, sec);
        else *pte=K2P(alloc_page_for_user())|PTE_USER_DATA;
    }
    else if (*pte & PTE_RO){
        auto p=alloc_page_for_user();
        memcpy(p,(void *)(CLEAN(*pte)),PAGE_SIZE);
        *pte=K2P(p)|PTE_USER_DATA;
    }
    else if (!(*pte&PTE_VALID)&&(sec->flags&ST_SWAP)){
        swapin(pd, sec);
    }
    attach_pgdir(pd);
    arch_tlbi_vmalle1is();
    return iss;
    // TODO
}