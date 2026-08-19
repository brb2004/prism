
    .globl f
    .text
f:
    pushq %rbp
    movq %rsp, %rbp
        movl $3, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret

    .globl main
    .text
main:
    pushq %rbp
    movq %rsp, %rbp
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        .section .note.GNU-stack,"",@progbits
