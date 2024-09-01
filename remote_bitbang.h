#ifndef REMOTE_BITBANG_H
#define REMOTE_BITBANG_H

#include <chrono>
#include <thread>
#include <stdint.h>
#include <sys/types.h>

#include "tap_state_machine.h"

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

    virtual void state_entered(tsm_state new_state) override;

private:

    // jtag test clock. System changes state on rising edge of the clock.
    unsigned char tck;

    // jtag test mode select. Makes the TAP's state machine transition.
    unsigned char tms;

    // jtag test data in. Data in from openocd.
    unsigned char tdi;

    // jtag TAP reset. optional TAP reset pin
    unsigned char trstn;

    // jtag test data out. openocd reads a byte of respone out of the DUT
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