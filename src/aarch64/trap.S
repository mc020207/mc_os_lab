#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
//TODO
pushp(x16,x17)
pushp(x14,x15)
pushp(x12,x13)
pushp(x10,x11)
pushp(x8,x9)
pushp(x6,x7)
pushp(x4,x5)
pushp(x2,x3)
pushp(x0,x1)
mrs x0,spsr_el1
mrs x1,elr_el1
pushp(x0,x1)
mov x0,sp
bl trap_global_handler
.global trap_return
trap_return:
//TODO
popp(x0,x1)
msr spsr_el1,x0
msr elr_el1,x1
popp(x0,x1)
popp(x2,x3)
popp(x4,x5)
popp(x6,x7)
popp(x8,x9)
popp(x10,x11)
popp(x12,x13)
popp(x14,x15)
popp(x16,x17)
eret