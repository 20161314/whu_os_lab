/* 实验手册任务4: 编写链接脚本 */
OUTPUT_ARCH(riscv)
ENTRY(_start) /* 作用是设置程序入口点为 _start 符号 */

/* 新增：定义程序头 (Program Headers) 来分离权限 */
PHDRS
{
  text PT_LOAD FLAGS(5);  /* 权限 R-X, 5 = Read(4) + Execute(1) */
  data PT_LOAD FLAGS(6);  /* 权限 RW-, 6 = Read(4) + Write(2) */
}

BASE_ADDRESS = 0x80000000; /* xv6将内核放在这个地址 */

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        *(.text .text.*)
    } :text /* 修改：将 .text section 映射到 text program header */

    .rodata : {
        . = ALIGN(16);
        *(.rodata .rodata.*)
    } :text /* 修改：将 .rodata section 也映射到 text program header */

    /* 定义 etext 等符号供代码使用 */
    PROVIDE(etext = .);

    .data : {
        . = ALIGN(16);
        *(.data .data.*)
    } :data /* 修改：将 .data section 映射到 data program header */

    .bss : {
        . = ALIGN(16);
        PROVIDE(bss_start = .);
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        PROVIDE(bss_end = .);
    } :data /* 修改：将 .bss section 也映射到 data program header */

    PROVIDE(end = .);
}
