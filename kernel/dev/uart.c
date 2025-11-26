/* 实验手册任务5: 实现串口驱动 */

// QEMU virt 机器的 UART 基地址 [cite: 18, 110]
#define UART_BASE 0x10000000L
// 这里相当于接口的相关知识，告诉我们接口的地址和寄存器

// THR: Transmit Holding Register (发送寄存器) [cite: 83]
#define UART_THR (unsigned char *)(UART_BASE + 0x00)
// THR代表发送保持寄存器，它位于基址偏移0x00处
// LSR: Line Status Register (线路状态寄存器) [cite: 84]
#define UART_LSR (unsigned char *)(UART_BASE + 0x05)
//LSR代表线路状态寄存器，它位于基址偏移0x05处
// LSR 的第5位 (bit 5) 是 THRE (Transmitter Holding Register Empty)
// 当它为1时，表示发送寄存器为空，可以写入下一个字符 [cite: 86]
#define LSR_THRE (1 << 5)//用于比较的一个值用来看能不能进行发送
//代表发射寄存器为空，可以接受并发送下一个字符。
// 发送一个字符 [cite: 90]
void uart_putc(char c) {
    // 等待发送寄存器为空 [cite: 86]
    while ((*UART_LSR & LSR_THRE) == 0);
    //一直进行轮询，直到空闲时发送
    *UART_THR = c;//将字符写入发送寄存器
}

// 发送一个以 '\0' 结尾的字符串 [cite: 92]
void uart_puts(char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}
