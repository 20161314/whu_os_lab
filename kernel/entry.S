# 实验手册任务1：实现启动汇编代码

.section .text # 声明这是代码段
.global _start # 声明 _start 为全局符号，作为程序入口点

_start:
    # 任务1 xv6的第一条指令就是设置栈指针
    # 我们也这样做，直接设置栈指针sp，不再需要gp的设置
    la sp, stack_top # 将栈顶地址加载到栈指针sp中
    # 清零 BSS 段
    # 符号 bss_start 和 bss_end 将在链接脚本中定义
    la a0, bss_start  # 加载BSS段起始地址到a0
    la a1, bss_end    # 加载BSS段结束地址到a1
    bge a0, a1, .L_bss_done # 如果起始地址>=结束地址，说明BSS为空，跳过清零
    #  在原版xv6中，调用了c函数来进行bss段的清零，我们在这里直接用汇编实现
# BSS清零循环
.L_bss_loop:
    sd zero, (a0)       # 存储0到当前地址
    addi a0, a0, 8      # 地址+8（64位=8字节）
    blt a0, a1, .L_bss_loop    # 如果未到结束地址，继续循环
.L_bss_done:

    # 任务3.4: 跳转到C主函数
    call main

    # 防止程序意外退出，进入死循环
halt:
    wfi      # Wait For Interrupt，让CPU休眠
    j halt   # 无限循环
    # 使cpu在这里休眠，等待中断

# 在 .bss 段中为启动栈预留空间，不生成可执行的diamagnetic
.section .bss          # BSS段声明
.align 16             # 16字节对齐
stack_bottom:         # 栈底标签
.space 4096           # 分配4KB栈空间
stack_top:            # 栈顶标签

