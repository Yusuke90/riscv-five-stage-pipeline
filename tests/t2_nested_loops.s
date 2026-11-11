# Inner loop of 3 iterations, run 4 times (outer loop of 4).
.text
.globl main
main:
    addi t0, zero, 4        # outer counter
    addi t2, zero, 0        # work counter
outer:
    addi t1, zero, 3        # inner counter
inner:
    addi t2, t2, 1
    addi t1, t1, -1
    bne  t1, zero, inner    # inner branch: T T N, four times
    addi t0, t0, -1
    bne  t0, zero, outer    # outer branch: T T T N
    addi a7, zero, 10
    ecall
