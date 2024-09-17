# Executed when the make command is used without options
# build the executable
.PHONY: all
all: example.elf hex listing dump_text_section

# compile the .c file into a .elf file
example.elf: example.c
	/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -g -v -march=rv32id -nostartfiles -x c -Ttext 40000000 -Tdata 50000000 -Tbss 60000000 -o example.elf example.c

# create a intel hex file
# View this hex file using this: http://www.dlwrr.com/electronics/tools/hexview/hexview.html
.PHONY: hex
hex:
	/opt/riscv/bin/riscv32-unknown-linux-gnu-objcopy -O ihex example.elf example.hex

# build a listing file (weird format)
.PHONY: listing
listing:
	/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -Wa,-adhln -g example.c > assembly_list.s

# print the .text section to the console
# .text contains the executable code
.PHONY: dump_text_section
dump_text_section:
	/opt/riscv/bin/riscv32-unknown-linux-gnu-objdump -S -j .text example.elf > example.objdump

.PHONY: clean
clean:
	rm *.o example.elf example.hex assembly_list.s a.out
