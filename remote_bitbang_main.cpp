#include <iostream>

#include "remote_bitbang.h"
#include "tap_state_machine.h"

int main() {

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