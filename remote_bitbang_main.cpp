#include <iostream>
#include <filesystem>
#include <fstream> 

#include "remote_bitbang.h"
#include "tap_state_machine.h"
#include "riscv_assembler/ihex_loader/ihex_loader.h"
#include "riscv_assembler/cpu/cpu.h"

int main() {

    std::cout << "Openocd JTAG bitbang sample target started ..." << std::endl;

    //
    // load ihex file
    //

    // std::string ihex_file = "create_binary_example/example.hex";
    // IHexLoader ihex_loader;

    // ihex_loader.load_ihex_file(ihex_file);
    // ihex_loader.debug_output(0x20);

    //std::string ihex_file = "test/resources/add_example.hex";
    std::string ihex_file = "loop_example/example.hex";

    IHexLoader ihex_loader;
    if (ihex_loader.load_ihex_file(ihex_file)) {
        return -1;
    }
    ihex_loader.debug_output(0x20);

    cpu_t cpu;
    cpu_init(&cpu);
    cpu.pc = ihex_loader.start_address;
    
    //cpu.memory = memory;
    cpu.segments = &(ihex_loader.segments);

    // // run the CPU
    // for (int i = 0; i < 100; i++) {
    //     if (cpu_step(&cpu)) {
    //         break;
    //     }
    // }

    extern tsm_state tsm_current_state;

    remote_bitbang_t remote_bitbang(3335, &cpu);

    unsigned char jtag_tck = 0;
    unsigned char jtag_tms = 0;
    unsigned char jtag_tdi = 0;
    unsigned char jtag_trstn = 0;
    unsigned char tag_tdo = 0;

    while (!remote_bitbang.done()) {
        remote_bitbang.tick(&jtag_tck, &jtag_tms, &jtag_tdi, &jtag_trstn, tag_tdo);
    }

    return 0;
}
