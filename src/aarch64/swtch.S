// Do kernel-mode context switch
// x0 (first parameter): new context ptr
// x1 (second parameter): addr to save old context ptr

#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

.globl swtch
swtch:
//TODO
pushp(x28,x29)
pushp(x26,x27)
pushp(x24,x25)
pushp(x22,x23)
pushp(x20,x21)
pushp(x1,x19)
pushp(lr,x0)
mov x2 , sp
str x2 , [x1]
mov sp , x0
popp(lr,x0)
popp(x1,x19)
popp(x20,x21)
popp(x22,x23)
popp(x24,x25)
popp(x26,x27)
popp(x28,x29)
ret