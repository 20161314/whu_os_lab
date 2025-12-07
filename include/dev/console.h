// include/console.h
//各个文件里的函数声明集中。
// --- printf.c ---
#include <stdarg.h>
#include "common.h"

void print_init(void);
void printf(const char *fmt, ...);
void clear_screen(void);
void panic(const char* warning);
void assert(bool condition, const char* warning);

// --- console.c ---
void consputc(char c);

// --- uart.c ---
void uart_init(void);
void uart_intr(void);
void uart_putc(char c);
void uart_puts(char *s);