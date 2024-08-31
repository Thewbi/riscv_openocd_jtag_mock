#include <iostream>

#include "remote_bitbang.h"

int main() {

    //std::cout << "Hello World!";

    remote_bitbang_t remote_bitbang(3335);

    unsigned char jtag_tck;
    unsigned char jtag_tms;
    unsigned char jtag_tdi;
    unsigned char jtag_trstn;
    unsigned char tag_tdo;

    while (!remote_bitbang.done()) {

        //fprintf(stderr, "tick\n");
        remote_bitbang.tick(&jtag_tck, &jtag_tms, &jtag_tdi, &jtag_trstn, tag_tdo);
    }

    return 0;
}