#include "dev/console.h"
#include "lib/lock.h"

// 静态辅助函数声明
static void printint(long long xx, int base, int sign);
static void printptr(unsigned long long x);

// 防止占用的lock
static struct {
  struct spinlock print_lk;
  int locking;
} pr;

void print_init(void)
{
  uart_init();
  spinlock_init(&pr.print_lk, "pr");
  pr.locking = 1;
}

// printint - 打印一个带符号的整数 (迭代实现，避免栈溢出)
// xx: 要打印的数字
// base: 进制 (例如 10 或 16)
// sign: 是否处理符号 (1 表示处理, 0 表示不处理)
static void printint(long long xx, int base, int sign) {
    char buf[32];
    int i = 0;
    unsigned long long x;

    // 处理负数，并解决 INT_MIN 溢出问题
    if (sign && xx < 0) {
        consputc('-');
        x = -xx;
    } else {
        x = xx;
    }

    // 使用迭代法将数字转换为字符串，逆序存入 buf
    do {
        buf[i++] = "0123456789abcdef"[x % base];
    } while ((x /= base) != 0);

    // 将 buf 中的字符正序输出
    while (--i >= 0) {
        consputc(buf[i]);
    }
}

// printptr - 打印一个指针地址
// 地址以 0x 开头的十六进制格式输出
static void printptr(unsigned long long x) {
    consputc('0');
    consputc('x');
    for (int i = 0; i < (sizeof(unsigned long long) * 2); i++, x <<= 4) {
        consputc("0123456789abcdef"[x >> (sizeof(unsigned long long) * 8 - 4)]);
    }
}


// printf - 内核格式化输出主函数
void printf(const char *fmt, ...) {
    va_list ap;
    char *s;
    int c, locking;

    locking = pr.locking;
    if(locking)
        spinlock_acquire(&pr.print_lk);

    if (fmt == 0) {
        return; // 处理空指针
    }

    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            consputc(*p);
            continue;
        }

        p++; // 跳过 '%'
        if (*p == '\0') {
             break; // 防止格式字符串以 '%' 结尾
        }

        switch (*p) {
            case 'd': // 整数
                printint(va_arg(ap, int), 10, 1);
                break;
            case 'x': // 十六进制
                printint(va_arg(ap, int), 16, 0);
                break;
            case 'p': // 指针
                printptr(va_arg(ap, unsigned long long));
                break;
            case 's': // 字符串
                if ((s = va_arg(ap, char *)) == 0) {
                    s = "(null)";
                }
                while (*s) {
                    consputc(*s++);
                }
                break;
            case 'c': // 字符
                c = va_arg(ap, int);
                consputc(c);
                break;
            case '%': // 百分号
                consputc('%');
                break;
            default: // 未知格式，直接打印
                consputc('%');
                consputc(*p);
                break;
        }
    }
    va_end(ap);

    if(locking)
        spinlock_release(&pr.print_lk);
}

// 清屏函数实现
void clear_screen(void) {
    // 发送ANSI转义序列: \033 是 ESC 的八进制表示
    // [2J 表示清除整个屏幕
    uart_puts("\033[2J"); 
    // [H 表示将光标移动到左上角
    uart_puts("\033[H");
    printf("Screen cleared!\n");
}

void panic(const char* warning){
    printf(warning);
    while(1){};
}

void assert(bool condition, const char* warning){
    if(!condition){
        panic(warning);
    }
}