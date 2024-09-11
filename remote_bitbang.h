#ifndef REMOTE_BITBANG_H
#define REMOTE_BITBANG_H

#include <chrono>
#include <thread>
#include <stdint.h>
#include <sys/types.h>
#include <string>
#include <inttypes.h>

#include "tap_state_machine.h"

// // instructions / register indexes
// enum class Instruction : uint8_t {
//     //UNKNOWN = 0x00,
//     IDCODE = 0x01,
//     BYPASS = 0xFF // bypass is all ones as defined inside the specification
// };

// RISCV Debug Extension defines a Debug Transport Module (DTM) which
// is the endpoint of the debug connection inside the chip.
//
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
    uint32_t id_code_shift_register{0}; // this is the IDCODE shift register

    // 6.1.4. DTM Control and Status (dtmcs, at 0x10)
    uint32_t dtmcs_container_register{0};
    uint32_t dtmcs_shift_register{0};

    // 6.1.5. Debug Module Interface Access (dmi, at 0x11)
    //
    // Via read and write operations to this register while specifying addresses
    // of registers inside the DM, the DM register can be read and written via
    // the DTM
    uint64_t dmi_container_register{0};
    uint64_t dmi_shift_register{0};

    //uint32_t status_container_register{0};
    //uint32_t status_shift_register{0};

    uint32_t abstractcs_container_register{0};
    uint32_t abstractcs_shift_register{0};

    uint8_t shift_amount{0};

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

    /// @brief Convert the operation into a string for debug output.
    /// @param dmi_op the operation used in the DMI DebugModuleInterfaceAccess register. See RISC-V debug specification page 95,96.
    /// @return a human readable name for the operation.
    std::string operation_as_string(uint64_t dmi_op);

    std::string dm_register_as_string(uint32_t address);

    //
    // these variables all belong to the register 0x10 == DebugModule Control Register (DebugSpec, Page 26 and Page 30)
    //

    uint32_t haltreq = 0x00; // writing 0 clears the halt request for all currently selected harts. This may cancal outstanding halt requests for those harts.
    uint32_t resumereq = 0x00;
    uint32_t hartreset = 0x00;
    uint32_t ackhavereset = 0x00;
    uint32_t ackunavail = 0x00;
    uint32_t hasel = 0x00;
    uint32_t hartsello = 0x00; // ??? which (hardware thread) is selected
    uint32_t hartselhi = 0x00;
    uint32_t setkeepalive = 0x00;
    uint32_t clrkeepalive = 0x00;
    uint32_t setresethaltreq = 0x00;
    uint32_t clrresethaltreq = 0x00;
    uint32_t ndmreset = 0x00;
    uint32_t dmactive = 0x00; // 0x00 module needs reset, 0x01 module functions normally.

    //
    // these variables all belong to the register 3.14.6. Abstract Control and Status (abstractcs, at 0x16)
    //

    // Writing this register while an abstract command is executing causes cmderr to become 1 (busy) once
    // the command completes (busy becomes 0).

    // progbufsize
    uint32_t progbufsize = 0x00;

    // 0 (ready): There is no abstract command currently being executed.
    // 1 (busy): An abstract command is currently being executed
    uint32_t busy = 0x00;

    // This optional bit controls whether program buffer and
    // abstract memory accesses are performed with the exact
    // and full set of permission checks that apply based on the
    // current architectural state of the hart performing the
    // access, or with a relaxed set of permission checks (e.g. PMP
    // restrictions are ignored). The details of the latter are
    // implementation-specific.
    // 0 (full checks): Full permission checks apply.
    // 1 (relaxed checks): Relaxed permission checks apply
    uint32_t relaxedpriv = 0x00;

    // Gets set if an abstract command fails. The bits in this field
    // remain set until they are cleared by writing 1 to them. No
    // abstract command is started until the value is reset to 0.
    // This field only contains a valid value if busy is 0.
    // 0 (none): No error.
    // 1 (busy): An abstract command was executing while
    // command, abstractcs, or abstractauto was written, or when
    // one of the data or progbuf registers was read or written.
    // This status is only written if cmderr contains 0.
    // 2 (not supported): The command in command is not
    // supported. It may be supported with different options set,
    // but it will not be supported at a later time when the hart or
    // system state are different.
    // 3 (exception): An exception occurred while executing the
    // command (e.g. while executing the Program Buffer).
    // 4 (halt/resume): The abstract command couldn’t execute
    // because the hart wasn’t in the required state
    // (running/halted), or unavailable.
    // 5 (bus): The abstract command failed due to a bus error
    // (e.g. alignment, access size, or timeout).
    // 6 (reserved): Reserved for future use.
    // 7 (other): The command failed for another reason.
    uint32_t cmderr = 0x00;

    // Number of data registers that are implemented as part of
    // the abstract command interface. Valid sizes are 1 — 12.
    uint32_t datacount = 0x00;

    std::string riscv_register_as_string(uint32_t register_index);

    // uint64_t abstract_data_0 = 0x00;
    // uint64_t abstract_data_1 = 0x00;
    // uint64_t abstract_data_2 = 0x00;
    // uint64_t abstract_data_3 = 0x00;
    // uint64_t abstract_data_4 = 0x00;
    // uint64_t abstract_data_5 = 0x00;
    // uint64_t abstract_data_6 = 0x00;
    // uint64_t abstract_data_7 = 0x00;
    // uint64_t abstract_data_8 = 0x00;
    // uint64_t abstract_data_9 = 0x00;
    // uint64_t abstract_data_10 = 0x00;
    // uint64_t abstract_data_11 = 0x00;

    uint64_t abstract_data[12]{0};

    uint64_t arg0 = 0x00;
    uint64_t arg1 = 0x00;
    uint64_t arg2 = 0x00;

};

#endif