.PHONY: all
all: a.out

a.out: remote_bitbang_main.cpp \
	remote_bitbang.h remote_bitbang.cpp \
	tap_state_machine.h tap_state_machine.cpp \
	tap_state_machine_callback.h tap_state_machine_callback.cpp \
	riscv_assembler/ihex_loader/ihex_loader.h riscv_assembler/ihex_loader/ihex_loader.cpp \
	riscv_assembler/cpu/cpu.h riscv_assembler/cpu/cpu.c \
	riscv_assembler/data/asm_line.h riscv_assembler/data/asm_line.c \
	riscv_assembler/decoder/decoder.h riscv_assembler/decoder/decoder.c
	g++ -g remote_bitbang_main.cpp \
	remote_bitbang.cpp \
	tap_state_machine.cpp \
	tap_state_machine_callback.cpp \
	riscv_assembler/ihex_loader/ihex_loader.cpp \
	riscv_assembler/cpu/cpu.c \
	riscv_assembler/data/asm_line.c \
	riscv_assembler/decoder/decoder.c

.PHONY: clean
clean:
	rm *.o a.out
