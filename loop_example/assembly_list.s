   1              		.file	"example.c"
   2              		.option nopic
   3              		.attribute arch, "rv32i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0"
   4              		.attribute unaligned_access, 0
   5              		.attribute stack_align, 16
   6              		.text
   7              	.Ltext0:
   8              		.file 0 "/home/wbi/dev/openocd/riscv_openocd_jtag_mock/loop_example" "example.c"
   9              		.align	1
  10              		.globl	main
  12              	main:
  13              	.LFB0:
  14              		.file 1 "example.c"
   1:example.c     **** // /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32id -g example.c -o example.elf
   2:example.c     **** // /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -v -march=rv32id -nostartfiles -x c -Ttext 40000000
   3:example.c     **** // /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -Wa,-adhln -g example.c > assembly_list.s
   4:example.c     **** // /opt/riscv/bin/riscv32-unknown-linux-gnu-objcopy -O ihex example.elf example.hex
   5:example.c     **** // /opt/riscv/bin/riscv32-unknown-linux-gnu-objdump -s -j .text /home/wbi/dev/openocd/riscv_openocd
   6:example.c     **** int main() {
  15              		.loc 1 6 12
  16              		.cfi_startproc
  17 0000 0111     		addi	sp,sp,-32
  18              		.cfi_def_cfa_offset 32
  19 0002 22CE     		sw	s0,28(sp)
  20              		.cfi_offset 8, -4
  21 0004 0010     		addi	s0,sp,32
  22              		.cfi_def_cfa 8, 0
   7:example.c     **** 
   8:example.c     ****     int i = 0;
  23              		.loc 1 8 9
  24 0006 232604FE 		sw	zero,-20(s0)
   9:example.c     ****     int j = 0;
  25              		.loc 1 9 9
  26 000a 232404FE 		sw	zero,-24(s0)
  10:example.c     ****     for (i = 0; i < 2; i++) {
  27              		.loc 1 10 12
  28 000e 232604FE 		sw	zero,-20(s0)
  29              		.loc 1 10 5
  30 0012 19A8     		j	.L2
  31              	.L3:
  11:example.c     ****         j = j + 1;
  32              		.loc 1 11 11
  33 0014 832784FE 		lw	a5,-24(s0)
  34 0018 8507     		addi	a5,a5,1
  35 001a 2324F4FE 		sw	a5,-24(s0)
  10:example.c     ****     for (i = 0; i < 2; i++) {
  36              		.loc 1 10 25 discriminator 3
  37 001e 8327C4FE 		lw	a5,-20(s0)
  38 0022 8507     		addi	a5,a5,1
  39 0024 2326F4FE 		sw	a5,-20(s0)
  40              	.L2:
  10:example.c     ****     for (i = 0; i < 2; i++) {
  41              		.loc 1 10 19 discriminator 1
  42 0028 0327C4FE 		lw	a4,-20(s0)
  43 002c 8547     		li	a5,1
  44 002e E3D3E7FE 		ble	a4,a5,.L3
  12:example.c     ****     }
  13:example.c     **** 
  14:example.c     ****     return j;
  45              		.loc 1 14 12
  46 0032 832784FE 		lw	a5,-24(s0)
  15:example.c     **** }...
  47              		.loc 1 15 1
  48 0036 3E85     		mv	a0,a5
  49 0038 7244     		lw	s0,28(sp)
  50              		.cfi_restore 8
  51              		.cfi_def_cfa 2, 32
  52 003a 0561     		addi	sp,sp,32
  53              		.cfi_def_cfa_offset 0
  54 003c 8280     		jr	ra
  55              		.cfi_endproc
  56              	.LFE0:
  58              	.Letext0:
