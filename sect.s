
    .globl boot
    .section ".limine_requests"
boot:
    pushq %rbp
    movq %rsp, %rbp
        movl $1, %eax
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
        call boot
    movq %rbp, %rsp
    popq %rbp
    ret
        movl $0, %eax
    movq %rbp, %rsp
    popq %rbp
    ret
        .section .note.GNU-stack,"",@progbits
