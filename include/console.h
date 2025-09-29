// include/console.h
//各个文件里的函数声明集中。
// --- printf.c ---
#include <stdarg.h>
void printf(const char *fmt, ...);
void clear_screen(void);

// --- console.c ---
void consputc(char c);

// --- uart.c ---
void uart_putc(char c);
void uart_puts(char *s);