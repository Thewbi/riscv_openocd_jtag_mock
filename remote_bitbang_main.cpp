#include <iostream>
#include <filesystem>
#include <fstream> 

#include "remote_bitbang.h"
#include "tap_state_machine.h"
#include "ihex_loader.h"

int main() {

    std::cout << "Openocd JTAG bitbang sample target started ..." << std::endl;

    //
    // load ihex file
    //

    std::string ihex_file = "create_binary_example/example.hex";

    // load a hex file so that openocd does not have to upload data 
    // since the bitbang protocol is very, very slow and it takes
    // over a minute to upload 1000 bytes.
    std::filesystem::path ihex_file_path = std::filesystem::path(ihex_file);

    if (!std::filesystem::exists(ihex_file_path)) {
        std::cout << ihex_file << " does not exist!" << std::endl;
        return -1;
    }
 
    std::ifstream ihex_file_ifstream = std::ifstream(ihex_file_path);

    // // DEBUG output ihex file line by line
    // std::string line;
    // while (getline(ihex_file_ifstream, line)) {
    //     std::cout << line << "\n";
    // }

    IHexLoader ihex_loader;

    std::string line;
    while (getline(ihex_file_ifstream, line)) {
        ihex_loader.process_hex_line(line);
    }

    std::cout << "START ADDRESS FOR PROGRAM COUNTER: " << std::setfill('0') << std::setw(8) << std::hex << ihex_loader.start_address << std::endl;

    std::map<uint32_t, uint32_t*>::iterator it;
    for (it = ihex_loader.segments.begin(); it != ihex_loader.segments.end(); it++)
    {
        std::cout << "\n" << std::setfill('0') << std::setw(8) << std::hex << it->first << std::endl;

        uint8_t column = 0;
        for (size_t i = 0; i < 0x4000; i++) {

            if (column == 0) {
                std::cout << "[" << std::setfill('0') << std::setw(8) << std::hex << i << "] ";
            }

            uint32_t data = it->second[i];
            std::cout << std::setfill('0') << std::setw(8) << std::hex << data << " ";

            column++;

            // if (i == 0x1F74/4) {
            //     std::cout << "boink" << std::endl;
            // }

            if (column == 8) {
                std::cout << std::endl;
                column = 0;
            }
        }
    }

    extern tsm_state tsm_current_state;

    remote_bitbang_t remote_bitbang(3335);

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
