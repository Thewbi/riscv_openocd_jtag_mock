#ifndef REMOTE_BITBANG_H
#define REMOTE_BITBANG_H

#include <chrono>
#include <thread>
#include <stdint.h>
#include <sys/types.h>

#include "tap_state_machine.h"

// instructions / register indexes
enum class Instruction : uint8_t {
    //UNKNOWN = 0x00,
    IDCODE = 0x01,
    BYPASS = 0xFF // bypass is all ones as defined inside the specification
};

class remote_bitbang_t : public TSMStateMachineCallback
{
public:
    // Create a new server, listening for connections from localhost on the given
    // port.
    remote_bitbang_t(uint16_t port);

    // Do a bit of work.
    void tick(unsigned char *jtag_tck,
              unsigned char *jtag_tms,
              unsigned char *jtag_tdi,
              unsigned char *jtag_trstn,
              unsigned char jtag_tdo);

    unsigned char done() { return quit; }

    int exit_code() { return err; }

    virtual void state_entered(tsm_state new_state, uint8_t rising_edge_clk) override;

private:

    // jtag test clock. System changes state on rising edge of the clock.
    unsigned char tck;

    // jtag test mode select. Makes the TAP's state machine transition.
    unsigned char tms;

    // jtag test data in. Data in from openocd.
    unsigned char tdi;

    // jtag TAP reset. optional TAP reset pin
    unsigned char trstn;

    // jtag test data out. openocd reads a bit of response out of the DUT
    unsigned char tdo;

    int err;

    unsigned char quit;

    // socket server variables
    int socket_fd;
    int client_fd;
    static const ssize_t buf_size = 64 * 1024;
    char recv_buf[buf_size];
    ssize_t recv_start, recv_end;

    TSMStateMachine tsm_state_machine;

    // this is IR
    uint8_t instruction_container_register{static_cast<uint8_t>(Instruction::IDCODE)}; // after reset, store IDCODE

    uint8_t instruction_shift_register{0};

    // this is the IDCODE container register which is indexed writing IDCODE into IR
    //uint32_t id_code_container_register = 0x05B4603F; // https://onlinedocs.microchip.com/oxy/GUID-C0DEC68F-9589-43E1-B26B-4C3E38933283-en-US-1/GUID-A95CFBC2-41D5-4755-AB8E-B4866693D026.html
    //uint32_t id_code_container_register = 0x20000c05; // https://community.platformio.org/t/openocd-flash-command-for-risc-v/26038/3
    uint32_t id_code_container_register = 0x20000913;
    
    // this is the IDCODE shift register
    uint32_t id_code_shift_register;

    /// @brief Check for a client connecting, and accept if there is one.
    void accept();

    /// @brief Execute any commands the client has for us. But we only execute 1 because we need time for the simulation to run.
    void execute_command();

    /// @brief TAP reset (trst) and system reset (srst). Signals trst and srst are active low.
    /// @param trst TAP reset. performs TAP reset. Makes the state machine go back to TEST_LOGIC_RESET and writes IDCODE into DR
    /// @param srst System rest. ??? no documentation found about what system reset does
    void reset(char trst, char srst);

    /// @brief
    /// @param _tck
    /// @param _tms
    /// @param _tdi
    void set_pins(char _tck, char _tms, char _tdi);
};

#endif