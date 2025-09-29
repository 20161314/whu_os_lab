#include "console.h"

// 声明在 uart.c 中定义的函数
void uart_puts(char *s);

// C代码入口函数，函数名需与 entry.S 中的 `call` 指令对应
void main() {
    int test_var = 42;
    int *ptr = &test_var;
    clear_screen();
    printf("====== Experiment 1 Test Start ======\n");
    uart_puts("Hello OS\n");
    printf("====== Experiment 1 Test Start ======\n");
    printf("====== Experiment 2 Test Start ======\n");
    printf("Testing integer: %d\n", 12345);
    printf("Testing negative integer: %d\n", -54321);
    printf("Testing zero: %d\n", 0);
    printf("Testing hex: 0x%x\n", 0xABCD);
    printf("Testing string: %s\n", "Hello, layered OS!");
    printf("Testing char: %c\n", 'Z');
    printf("Testing percent: %%\n");

    // 测试指针打印
    printf("Testing pointer address: %p\n", ptr);
    printf("Testing NULL pointer: %p\n", (void *)0);

    printf("====== Experiment 2 Test End ======\n");

    // 任务完成后，进入死循环
    while (1);
}