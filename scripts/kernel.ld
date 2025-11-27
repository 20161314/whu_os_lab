/* 实验手册任务4: 编写链接脚本 */
OUTPUT_ARCH(riscv)
ENTRY(_start) /* 作用是设置程序入口点为 _start 符号 [cite: 33] */

BASE_ADDRESS = 0x80000000; /* xv6将内核放在这个地址 [cite: 34] */

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        *(.text .text.*)
        . = ALIGN(0x1000);

        _trampoline = .;
        *(trampsec)
        . = ALIGN(0x1000);
        ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");

        PROVIDE(etext = .);
        PROVIDE(KERNEL_DATA = .);
    } /* 修改：将 .text section 映射到 text program header */

    .rodata : {
        . = ALIGN(16);
        *(.rodata .rodata.*)
    } /* 修改：将 .rodata section 也映射到 text program header */    

    .data : {
        . = ALIGN(16);
        *(.data .data.*)
    } /* 修改：将 .data section 映射到 data program header */

    .bss : {
        . = ALIGN(16);
        PROVIDE(bss_start = .);
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        PROVIDE(bss_end = .);
    }/* 修改：将 .bss section 也映射到 data program header */


    PROVIDE(end = .);

    /* 对齐到页边界，并定义 ALLOC_BEGIN */
    . = ALIGN(0x1000);
    PROVIDE(ALLOC_BEGIN = .);
}
