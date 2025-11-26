/* 实验手册任务4: 编写链接脚本 */
OUTPUT_ARCH(riscv)
ENTRY(_start) /* 作用是设置程序入口点为 _start 符号 [cite: 33] */

BASE_ADDRESS = 0x80000000; /* xv6将内核放在这个地址 [cite: 34] */

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        *(.text .text.*)
    } 

    .rodata : {
        . = ALIGN(16);
        *(.rodata .rodata.*)
    } 

    /* 定义 etext, edata, end 等符号供代码使用 [cite: 35] */
    PROVIDE(etext = .);

    .data : {
        . = ALIGN(16);
        *(.data .data.*)
    } 

    .bss : {
        . = ALIGN(16);
        PROVIDE(bss_start = .);
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        PROVIDE(bss_end = .);
    } 

    PROVIDE(end = .);
}
