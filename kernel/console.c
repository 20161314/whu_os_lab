#include <console.h>
// consputc - 控制台字符输出函数
// 这是 printf 和底层硬件驱动之间的中间人。
// 目前，它只是简单地调用 uart_putc。
// 未来，这里可以添加缓冲区、锁，并决定将字符发送到多个设备（如屏幕）。
void consputc(char c) {
    uart_putc(c);
}