
    .globl main
    .text
main:
    pushq %rbp
    movq %rsp, %rbp
        movl $5, %ecx
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        .section .note.GNU-stack,"",@progbits
