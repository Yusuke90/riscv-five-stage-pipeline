   .text
   .globl main
   main:
       li   gp, buf
       addi t0, zero, -8
       srai t1, t0, 1
       srli t2, t0, 28
       slt  t3, t0, zero
       sltu t4, t0, zero
       addi t5, zero, 133
       sb   t5, 0(gp)
       lb   s0, 0(gp)
       lbu  s1, 0(gp)
       sh   t5, 2(gp)
       lh   s2, 2(gp)
       jal  ra, fn
       addi a7, zero, 10
       ecall
   fn:
       addi s3, zero, 7
       ret
   .data
   buf: .word 0