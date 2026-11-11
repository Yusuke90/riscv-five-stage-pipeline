# Stand-in for the course's t4: recursive calls (jal) and returns (jalr).
# f(n) recurses n levels deep; main calls f(8) twice.
.text
.globl main
main:
    li   sp, 8192
    addi a0, zero, 8
    jal  ra, f
    addi a0, zero, 8
    jal  ra, f
    addi a7, zero, 10
    ecall
f:
    beq  a0, zero, base     # forward branch: taken only at the bottom
    addi sp, sp, -8
    sw   ra, 0(sp)
    addi a0, a0, -1
    jal  ra, f
    lw   ra, 0(sp)
    addi sp, sp, 8
base:
    ret
