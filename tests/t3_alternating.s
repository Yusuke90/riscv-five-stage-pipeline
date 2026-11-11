# A forward branch whose outcome alternates T N T N ... inside an 8-iteration loop.
.text
.globl main
main:
    addi t0, zero, 8        # iterations
loop:
    beq  t1, zero, evenp    # forward: taken on even iterations (T N T N ...)
    addi t3, t3, 1          # odd path
    j    join               # jal x0 (skips the even block)
evenp:
    addi t4, t4, 1          # even path
join:
    xori t1, t1, 1          # toggle
    addi t0, t0, -1
    bne  t0, zero, loop     # backward loop branch
    mv   a0, t3
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
