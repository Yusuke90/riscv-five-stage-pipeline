.text
.globl main
main:
    li   gp, data
    lw   t0, 0(gp)        # t0 = 5
    addi t1, t0, 1        # ALU operand (load-use)
    lw   t2, 0(gp)
    sw   t2, 4(gp)        # store data (load-use)
    lw   t3, 4(gp)
    lw   t4, 0(gp)
    beq  t4, t3, ok       # branch operand (load-use), taken
    addi s0, zero, 99     # must be squashed
ok:
    lw   s1, 8(gp)        # s1 = address of data
    lw   s2, 0(s1)        # address base (load-use)
    addi a7, zero, 10
    ecall
.data
data:
    .word 5
    .word 0
    .word data