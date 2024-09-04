#ifndef REMOTE_BITBANG_H
#define REMOTE_BITBANG_H

#include <chrono>
#include <thread>
#include <stdint.h>
#include <sys/types.h>

#include "tap_state_machine.h"

// // instructions / register indexes
// enum class Instruction : uint8_t {
//     //UNKNOWN = 0x00,
//     IDCODE = 0x01,
//     BYPASS = 0xFF // bypass is all ones as defined inside the specification
// };

// RISCV Debug Extension defines a Debug Transport Module (DTM) which
// terminates the debug connection inside the chip.
// It contains several registers, which are named by this enumeration.
//
// This enum implements page 93, table 16 - JTAG DTM TAP Registers from version 1.0.0-rc3
//
// The RISC-V Debug Specification's DTM is constructed on no protocol in particular.
// To keep the specification practical, the example implementation is based on the
// JTAG protocol. In the JTAG variant, the RISC-V DTM is also a JTAG TAP at the same time!
// Therefore this enumeration mixes TAP and DTM registers!
enum class RiscV_DTM_Registers : uint8_t
{
    // this JTAG register originates from the fact that the DTM in this implementation is using the JTAG protocol.
    JTAG_RISCV_BYPASS = 0x00,
    JTAG_IDCODE = 0x01, // instruction 0x00001 is IDCODE as defined in the RISC-V DEBUG Specification (section 6.1.2).

    // RISCV DTM_CONTROL_AND_STATUS (dtmcs). Section 6.1.4 in the RISC-V DEBUG Specification.
    DTM_CONTROL_AND_STATUS = 0x10,
    DEBUG_MODULE_INTERFACE_ACCESS = 0x11,

    RESERVED_BYPASS_A = 0x12,
    RESERVED_BYPASS_B = 0x13,
    RESERVED_BYPASS_C = 0x14,
    RESERVED_BYPASS_D = 0x15,
    RESERVED_BYPASS_E = 0x16,
    RESERVED_BYPASS_F = 0x17,

    JTAG_SECOND_BYPASS = 0x1f,

    JTAG_BYPASS = 0xFF // bypass is all ones as defined inside the specification
};

class remote_bitbang_t : public TSMStateMachineCallback
{

public: 

    // The DMI uses between 7 and 32 address bits.
    // Each address points at a single 32-bit register that can be read or written.
    const uint8_t ABITS_LENGTH = 7;
    const uint16_t ABITS_MASK = 0b1111111;
    // const uint8_t ABITS_LENGTH = 16;
    // const uint16_t ABITS_MASK = 0b1111111111111111;

public:

    /// @brief Constructor. Creates a new server, listening for connections from localhost on the given
    // port.
    /// @param port the port where the server listens on for incoming JTAG bitbang connections (from openocd for example)
    remote_bitbang_t(uint16_t port);

    /// @brief Called by the driver (main()) in an endless loop as long as the server has not received
    /// a quit command. Acts as the interface between the verilator implementation and the JTAG server.
    /// mainly calls execute_command() which parses the incoming command and executes specific handlers.
    ///
    /// @param jtag_tck out - JTAG bitbang 'clock' signal sent by the JTAG client. Goes out to the FPAG system.
    /// @param jtag_tms out - JTAG bitbang 'mode select' signal sent by the JTAG client. Goes out to the FPAG system.
    /// @param jtag_tdi out - JTAG bitbang 'data in' signal sent by the JTAG client. Goes out to the FPAG system.
    /// @param jtag_trstn out - JTAG bitbang 'reset' signal sent by the JTAG client. Goes out to the FPAG system.
    /// @param jtag_tdo in - if the FPGA system wants to send a byte back over JTAG, this is set to true
    void tick(unsigned char *jtag_tck,
              unsigned char *jtag_tms,
              unsigned char *jtag_tdi,
              unsigned char *jtag_trstn,
              unsigned char jtag_tdo);

    unsigned char done() { return quit; }

    int exit_code() { return err; }

    /// @brief Callback from the state machine. Called as the state machine enters a new state.
    /// @param new_state the new state
    /// @param rising_edge_clk 
    virtual void state_entered(tsm_state new_state, uint8_t rising_edge_clk) override;

private:

    // jtag test clock. System changes state on rising edge of the clock.
    unsigned char tck;

    // jtag test mode select. Makes the TAP's state machine transition.
    unsigned char tms;

    // jtag test data in. Data in from openocd.
    uint64_t tdi;

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
    uint8_t instruction_container_register{static_cast<uint8_t>(RiscV_DTM_Registers::JTAG_IDCODE)}; // after reset, store IDCODE

    uint8_t instruction_shift_register{0};

    // this is the IDCODE container register which is indexed writing IDCODE into IR
    // uint32_t id_code_container_register = 0x05B4603F; // https://onlinedocs.microchip.com/oxy/GUID-C0DEC68F-9589-43E1-B26B-4C3E38933283-en-US-1/GUID-A95CFBC2-41D5-4755-AB8E-B4866693D026.html
    // uint32_t id_code_container_register = 0x20000c05; // https://community.platformio.org/t/openocd-flash-command-for-risc-v/26038/3
    uint32_t id_code_container_register = 0x20000913;
    uint32_t id_code_shift_register; // this is the IDCODE shift register

    // 6.1.4. DTM Control and Status (dtmcs, at 0x10)
    uint32_t dtmcs_container_register;
    uint32_t dtmcs_shift_register;

    // 6.1.5. Debug Module Interface Access (dmi, at 0x11)
    uint64_t dmi_container_register;
    uint64_t dmi_shift_register;

    uint8_t first_shift{1};

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

    /// @brief output dtmcs to the console
    /// @param dtmcs
    void print_dtmcs(uint32_t dtmcs);

    /// @brief Called once. Sets default values into the DTM Control and Status (dtmcs) register.
    /// See RISC-V Debug Specification, section 6.1.4, page 93.
    uint32_t init_dtmcs();

    void print_dmi(uint64_t dmi);

    uint64_t init_dmi();

    uint64_t get_dmi_address(uint64_t dmi);
    uint64_t get_dmi_data(uint64_t dmi);
    uint64_t get_dmi_op(uint64_t dmi);
};

#endif