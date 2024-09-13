   1              		.file	"example.c"
   2              		.option nopic
   3              		.attribute arch, "rv32i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0"
   4              		.attribute unaligned_access, 0
   5              		.attribute stack_align, 16
   6              		.text
   7              	.Ltext0:
   8              		.file 0 "/home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example" "example.c"
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
   8:example.c     ****     int anumber = 5;
  23              		.loc 1 8 9
  24 0006 9547     		li	a5,5
  25 0008 2326F4FE 		sw	a5,-20(s0)
   9:example.c     ****     int another = 30;
  26              		.loc 1 9 9
  27 000c F947     		li	a5,30
  28 000e 2324F4FE 		sw	a5,-24(s0)
  10:example.c     **** 
  11:example.c     ****     int result = anumber + another;
  29              		.loc 1 11 9
  30 0012 0327C4FE 		lw	a4,-20(s0)
  31 0016 832784FE 		lw	a5,-24(s0)
  32 001a BA97     		add	a5,a4,a5
  33 001c 2322F4FE 		sw	a5,-28(s0)
  12:example.c     **** 
  13:example.c     ****     return result;
  34              		.loc 1 13 12
  35 0020 832744FE 		lw	a5,-28(s0)
  14:example.c     **** }...
  36              		.loc 1 14 1
  37 0024 3E85     		mv	a0,a5
  38 0026 7244     		lw	s0,28(sp)
  39              		.cfi_restore 8
  40              		.cfi_def_cfa 2, 32
  41 0028 0561     		addi	sp,sp,32
  42              		.cfi_def_cfa_offset 0
  43 002a 8280     		jr	ra
  44              		.cfi_endproc
  45              	.LFE0:
  47              	.Letext0:
