/* 实验手册任务4: 编写链接脚本 */
OUTPUT_ARCH(riscv)     # 指定输出架构为RISC-V
ENTRY(_start)          # 设置程序入口点为_start符号（设置程序的入口点，同时声明_start是第一个运行的程序）


/* 新增：定义程序头 (Program Headers) 来分离权限 */
PHDRS
{
  text PT_LOAD FLAGS(5);  /* 权限 R-X, 5 = Read(4) + Execute(1) */
  data PT_LOAD FLAGS(6);  /* 权限 RW-, 6 = Read(4) + Write(2) */
}

BASE_ADDRESS = 0x80000000;  # 设置内核加载的基地址（0x80000000是内核加载的起始物理地址。将代码段的起始地址设置于此，确保了内核能被正确加载和执行
）

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
    PROVIDE(edata = .);
    .bss : {
        . = ALIGN(16);
        PROVIDE(bss_start = .);
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        PROVIDE(bss_end = .);
    } :data /* 修改：将 .bss section 也映射到 data program header */

    PROVIDE(end = .);
}
