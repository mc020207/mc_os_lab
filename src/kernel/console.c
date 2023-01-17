#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<driver/interrupt.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
} input;
#define C(x)      ((x) - '@')  // Control-x
#define BACKSPACE 0x100
static SpinLock conslock;
static Semaphore conssem;
define_early_init(conslock){
    init_spinlock(&conslock);
    init_sem(&conssem,0);
}
void console_init()
{
    init_spinlock(&conslock);
    init_sem(&conssem, 0);

    set_interrupt_handler(IRQ_AUX, console_intr2);
}
static void putc(int c){
    if (c==BACKSPACE){
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
    }else{
        uart_put_char(c);
    }
}
isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);
    _acquire_spinlock(&conslock);
    for (isize i=0;i<n;i++){
        putc(buf[i]);
    }
    _release_spinlock(&conslock);
    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    inodes.unlock(ip);
    _acquire_spinlock(&conslock);
    isize m=n;
    while (n>0){
        while (input.r==input.w){
            if (thisproc()->killed){
                _release_spinlock(&conslock);
                inodes.lock(ip);
                return -1;
            }
            _release_spinlock(&conslock);
            unalertable_wait_sem(&conssem);
        }
        int c=input.buf[input.r%INPUT_BUF];
        input.r+=1;
        if (c==C('D')){
            if (n<m) input.r--;
            break;
        }
        *dst=c;
        dst++;
        --n;
        if (c=='\n') break;
    }
    _release_spinlock(&conslock);
    inodes.lock(ip);
    return m-n;
}
void console_intr2(){
    console_init(uart_get_char);
}
void console_intr(char (*getc)()) {
    // TODO
    int c=0;
    _acquire_spinlock(&conslock);
    while ((c=(int)(getc()))&&c>=0){
        if (c==C('c')){
            ASSERT(kill(thisproc()->pid)==0);
        }else if (c==C('U')){
            while (input.e!=input.w&&input.buf[(input.e-1)%INPUT_BUF]!='\n'){
                input.e--;
                putc(BACKSPACE);
            }
        }else if (c=='\x7f'){
            if (input.e!=input.w){
                input.e--;
                putc(BACKSPACE);
            }
        }else{
            if (c!=0&&input.e-input.r<INPUT_BUF){
                // attention
                c=(c=='\r')?'\n':c;
                input.buf[input.e%INPUT_BUF]=c;
                input.e++;
                putc(c);
                if (c=='\n'||c==C('D')||input.e==input.r+INPUT_BUF){
                    input.w=input.e;
                    _post_sem(&conssem);
                }
            }
        }
    }
}
