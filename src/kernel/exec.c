#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/printk.h>

// static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

int execve(const char *path, char *const argv[], char *const envp[]) {
	// TODO
	struct proc* curproc=thisproc();
	struct pgdir oldpigdir=curproc->pgdir;
	struct pgdir* pgdir=kalloc(sizeof(struct pgdir));
	init_pgdir(pgdir);
	// printk("in execve:%p\n",curproc);
	Inode* ip=NULL;
	if (pgdir==NULL){
		goto bad;
	}
	OpContext ctx;
	bcache.begin_op(&ctx);
	ip=namei(path,&ctx);
	ASSERT(ip);
	if (ip==NULL){
		bcache.end_op(&ctx);
		goto bad;
	}
	inodes.lock(ip);
	Elf64_Ehdr elf;
	if (inodes.read(ip,(u8*)&elf,0,sizeof(elf))!=sizeof(elf)){
		goto bad;
	}
	if (!(elf.e_ident[EI_MAG0] == ELFMAG0 && elf.e_ident[EI_MAG1] == ELFMAG1
         && elf.e_ident[EI_MAG2] == ELFMAG2
         && elf.e_ident[EI_MAG3] == ELFMAG3)) {
        goto bad;
    }
	if (elf.e_ident[EI_CLASS] != ELFCLASS64) {
        goto bad;
    }
	int i=0;
	uint64_t off;
	Elf64_Phdr ph;
	curproc->pgdir=*pgdir;
	u64 sz=0,base=0,stksz=0;
	int first=1;
	// printk("elf.e_phnum:%d\n",elf.e_phnum);
	for(i=0,off=elf.e_phoff;i<elf.e_phnum;i++,off+=sizeof(ph)) {
		if ((inodes.read(ip,(u8*)&ph,off,sizeof(ph)))!=sizeof(ph)){
			PANIC();
		}
		if (ph.p_type!=PT_LOAD){
			continue;
		}
		if (ph.p_memsz < ph.p_filesz) {
            PANIC();
        }
        if (ph.p_vaddr + ph.p_memsz < ph.p_vaddr) {
            PANIC();
        }
		if (first){
			first=0;
			sz=base=ph.p_vaddr;
			if (base%PAGE_SIZE!=0){
				PANIC();
			}
		}
		if ((sz=uvm_alloc(pgdir,base,stksz,sz,ph.p_vaddr + ph.p_memsz))==0){
			PANIC();
		}
		attach_pgdir(pgdir);
		// printk("filesz:%lld\n",(u64)ph.p_filesz);
		// attention
    	arch_tlbi_vmalle1is();
		if (inodes.read(ip, (u8 *)ph.p_vaddr, ph.p_offset, ph.p_filesz)!=ph.p_filesz){
			PANIC();
		}
		memset((void *)ph.p_vaddr+ph.p_filesz,0,ph.p_memsz-ph.p_filesz);
		arch_fence();
		arch_dccivac((void*)ph.p_vaddr,ph.p_memsz);
		arch_fence();
	}
	inodes.unlock(ip);
	inodes.put(&ctx,ip);
	bcache.end_op(&ctx);
	ip=NULL;
	attach_pgdir(&oldpigdir);
	arch_tlbi_vmalle1is();
	char *sp=(char*)USERTOP;
	int argc=0,envc=0;
	usize len;
	if (argv){
		for (;argc<MAXARG&&argv[argc];argc++){
			len=strlen(argv[argc]);
			sp-=len+1;
			// sp-=sp%16;
			copyout(pgdir,sp,argv[argc],len+1);
		}
	}
	if (envp){
		for (;envc<MAXARG&&envp[envc];envc++){
			len=strlen(envp[envc]);
			sp-=len+1;
			copyout(pgdir,sp,envp[envc],len+1);
		}
	}
	void *newsp=(void*)(((usize)sp-(envc+argc+4)*8)/16*16);
	copyout(pgdir,newsp,NULL,(void*)sp-newsp);
	attach_pgdir(pgdir);
	arch_tlbi_vmalle1is();
	uint64_t *newargv=newsp+8;
    uint64_t *newenvp=(void*)newargv+8*(argc+1);
	// printk("in exec1: proc%p ucontext:%p elr:%p\n",curproc,curproc->ucontext ,(void*)thisproc()->ucontext->elr);
	for (int i=envc-1;i>=0;i--){
		newenvp[i]=(uint64_t)sp;
		for(;*sp;sp++);
		sp++;
	}
	for (int i=argc-1;i>=0;i--){
		newargv[i]=(uint64_t)sp;
		for(;*sp;sp++);
		sp++;
	}
	*(usize*)(newsp)=argc;
	sp=newsp;
	stksz=(USERTOP - (usize)sp+10*PAGE_SIZE-1)/(10*PAGE_SIZE)*(10*PAGE_SIZE);
	copyout(pgdir,(void *)(USERTOP - stksz),0, stksz - (USERTOP - (usize)sp));
	ASSERT((uint64_t) sp > USERTOP - stksz);
	curproc->pgdir=*pgdir;
	curproc->ucontext->elr=elf.e_entry;
	curproc->ucontext->sp=(uint64_t)sp;
	attach_pgdir(&curproc->pgdir);
	arch_tlbi_vmalle1is();
	free_pgdir(&oldpigdir);
	// printk("in exec1: proc%p ucontext:%p elr:%p\n",curproc,curproc->ucontext ,(void*)thisproc()->ucontext->elr);
	return 0;

bad:
	if (pgdir) free_pgdir(pgdir);
	if (ip){
		inodes.unlock(ip);
		inodes.put(&ctx,ip);
		bcache.end_op(&ctx);
	}
	thisproc()->pgdir=oldpigdir;
	printk("donghongleshabi");
	return -1;
}
