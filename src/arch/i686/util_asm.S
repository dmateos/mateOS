.section .rodata
my_str:
    .string "hello world\n"

.section .text

.global test_assembly
test_assembly:
    pushl %ebp
    movl %esp, %ebp
    subl $20, %esp

    pushl $my_str
    call printf

    movl %ebp, %esp
    popl %ebp
    ret

.global halt_and_catch_fire
halt_and_catch_fire:
    hlt
    ret

.global flush_gdt
flush_gdt:
    movl 4(%esp), %eax  #Load address of gdt into eax
    lgdt (%eax)
    ljmp $0x08, $next
next:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret

.global enable_paging
enable_paging:
    # Get page directory address from stack
    movl 4(%esp), %eax

    # Load page directory into CR3
    movl %eax, %cr3

    # Enable paging by setting bit 31 (PG) of CR0
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    ret

.global get_cr2
get_cr2:
    movl %cr2, %eax
    ret

.global get_cr3
get_cr3:
    movl %cr3, %eax
    ret
