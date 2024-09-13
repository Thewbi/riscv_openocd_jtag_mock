// /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32id -g example.c -o example.elf
// /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -v -march=rv32id -nostartfiles -x c -Ttext 40000000 -Tdata 50000000 -Tbss 60000000 -o example.elf example.c
// /opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -Wa,-adhln -g example.c > assembly_list.s
// /opt/riscv/bin/riscv32-unknown-linux-gnu-objcopy -O ihex example.elf example.hex
// /opt/riscv/bin/riscv32-unknown-linux-gnu-objdump -s -j .text /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf
int main() {

    int anumber = 5;
    int another = 30;

    int result = anumber + another;

    return result;
}