/* 实验手册任务6: 完成C主函数 */

// 声明在 uart.c 中定义的函数
void uart_puts(char *s);

// C代码入口函数，函数名需与 entry.S 中的 `call` 指令对应
void main() {
    // 阶段3: 功能验证，实现完整的Hello输出 [cite: 106]
    uart_puts("Hello 05\n");

    // 任务完成后，进入死循环，避免CPU失控
    while (1);
}
