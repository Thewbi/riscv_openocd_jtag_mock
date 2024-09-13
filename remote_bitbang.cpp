#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "remote_bitbang.h"

/// @brief constructor
/// @param port the port for the server to listen on for new connections.
remote_bitbang_t::remote_bitbang_t(uint16_t port) : socket_fd(0),
                                                    client_fd(0),
                                                    recv_start(0),
                                                    recv_end(0),
                                                    err(0),
                                                    tsm_state_machine(this)
{
    dtmcs_container_register = init_dtmcs();
    dmi_container_register = init_dmi();

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        fprintf(stderr, "remote_bitbang failed to make socket: %s (%d)\n", strerror(errno), errno);
        abort();
    }
    fcntl(socket_fd, F_SETFL, O_NONBLOCK);
    int reuseaddr = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1)
    {
        fprintf(stderr, "remote_bitbang failed setsockopt: %s (%d)\n", strerror(errno), errno);
        abort();
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        fprintf(stderr, "remote_bitbang failed to bind socket: %s (%d)\n", strerror(errno), errno);
        abort();
    }
    if (listen(socket_fd, 1) == -1)
    {
        fprintf(stderr, "remote_bitbang failed to listen on socket: %s (%d)\n", strerror(errno), errno);
        abort();
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(socket_fd, (struct sockaddr *)&addr, &addrlen) == -1)
    {
        fprintf(stderr, "remote_bitbang getsockname failed: %s (%d)\n", strerror(errno), errno);
        abort();
    }
    tck = 1;
    tms = 1;
    tdi = 1;
    trstn = 1;
    quit = 0;
    fprintf(stderr, "This emulator compiled with JTAG Remote Bitbang client. To enable, use +jtag_rbb_enable=1.\n");
    fprintf(stderr, "Listening on port %d\n", ntohs(addr.sin_port));
}

void remote_bitbang_t::accept()
{
    int again = 1;
    while (again != 0)
    {
        fprintf(stderr, "Attempting to accept client socket\n");

        client_fd = ::accept(socket_fd, NULL, NULL);
        if (client_fd == -1)
        {
            if (errno == EAGAIN)
            {
                // no client waiting to connect right now.
                fprintf(stderr, "Sleep 600ms\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
            }
            else
            {
                fprintf(stderr, "failed to accept on socket: %s (%d)\n", strerror(errno), errno);
                again = 0;
                abort();
            }
        }
        else
        {
            fcntl(client_fd, F_SETFL, O_NONBLOCK);
            again = 0;

            fprintf(stderr, "Accepted successfully\n");
        }
    }
}

void remote_bitbang_t::tick(
    unsigned char *jtag_tck,   // output from the JTAG module towards the rest of the FPGA modules
    unsigned char *jtag_tms,   // output from the JTAG module towards the rest of the FPGA modules
    unsigned char *jtag_tdi,   // output from the JTAG module towards the rest of the FPGA modules
    unsigned char *jtag_trstn, // optional TAP reset pin
    unsigned char jtag_tdo     // input from the FPGA system into the JTAG module to transfer back to the client
)
{
    if (client_fd > 0)
    {
        // should the client send the JTAG bitbang 'R' command, 
        // then it wants to read the current value of the tdo variable

        // Currently do not use the value that the driver (main()) 
        // program supplies since it currently is not connected to any functioning logic at all.
        // tdo = jtag_tdo;

        execute_command();
    }
    else
    {
        fprintf(stderr, "tick() accept()\n");
        this->accept();
    }

    // write values into the variables that the FPGA simulator can access via VHD

    // inform the FPGA system about the current "clock" signal
    *jtag_tck = tck;

    // inform the FPGA system about the current "mode select" signal
    *jtag_tms = tms;

    // inform the FPGA system about the current "data in" signal
    *jtag_tdi = tdi;

    // inform the FPGA system about the current "TAP reset" signal
    *jtag_trstn = trstn;
}

void remote_bitbang_t::reset(char trst, char srst)
{
    // trstn = 0;
    tsm_state_machine.tsm_reset();
}

// 0 - Write tck:0 tms:0 tdi:0
// 1 - Write tck:0 tms:0 tdi:1
// 2 - Write tck:0 tms:1 tdi:0
// 3 - Write tck:0 tms:1 tdi:1
// 4 - Write tck:1 tms:0 tdi:0
// 5 - Write tck:1 tms:0 tdi:1
// 6 - Write tck:1 tms:1 tdi:0
// 7 - Write tck:1 tms:1 tdi:1
void remote_bitbang_t::set_pins(char _tck, char _tms, char _tdi)
{
    //fprintf(stderr, "set_pins() retrieved input: tck:%d, tms:%d, tdi:%d\n", _tck, _tms, _tdi);

    tck = _tck;
    tms = _tms;
    tdi = _tdi;

    // on the rising edge, the TAP state machine transitions
    if (_tck != 0) {

        tsm_state_machine.transition(_tms, static_cast<uint8_t>(_tck));

    } else {

        //fprintf(stderr, "<< %d. ", tdi);

        switch (tsm_state_machine.tsm_current_state)
        {

            // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO
            // (on the falling edge of TCK) from the currently selected data or instruction register respectively.
            case SHIFT_DR:
                //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. ", rising_edge_clk);
                //fprintf(stderr, "SHIFT_DR entered.\n");

                // shift the data shift register which places the rightmost bit into tdo for subsequent reads to pick up.
                switch (static_cast<RiscV_DTM_Registers>(instruction_container_register))
                {

                // DebugSpec, Page 93 - IDCODE register - To identify a specific silicon version.
                case RiscV_DTM_Registers::JTAG_IDCODE:

                    //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. SHIFT ", rising_edge_clk);

                    // ignore first shift (dtmcontrol_scan_via_bscan() inside openocd source code: 
                    // "Note the starting offset is bit 1, not bit 0. In BSCAN tunnel, there is a one-bit TCK skew between output and input")
                    if (shift_amount >= 1) {

                        // prepare send (which happens on the falling edge of the clock)
                        // preload the pin with the value that is shifted out from the shift register
                        tdo = id_code_shift_register & 0x01;

                        // shift data from the tdi register in
                        id_code_shift_register = id_code_shift_register >> 1;

                        // shift in tdi from the left
                        id_code_shift_register |= (tdi << (32 - 1)); // size of idcode container register is 32 bits

                    }
                    shift_amount++;
                    break;

                // case Instruction::BYPASS:
                //     fprintf(stderr, "[Error] Unknown instruction register!!! BYPASS\n");
                //     break;

                // DebugSpec, Page 
                case RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS:
                    // fprintf(stderr, "SHIFTING dtcms\n");

                    // ignore first shift (dtmcontrol_scan_via_bscan()Note the starting offset is bit 1, not bit 0.  In BSCAN tunnel, there is a one-bit TCK skew between output and input)
                    if (shift_amount >= 1) {

                        // preload bit to send
                        tdo = dtmcs_shift_register & 0x01;

                        // shift data from the tdi register in
                        dtmcs_shift_register = dtmcs_shift_register >> 1;

                        // shift in tdi from the left
                        dtmcs_shift_register |= (tdi << (32 - 1)); // size of dtmcs container register is 32 bits
                    }
                    shift_amount++;
                    break;

                case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:
                    //fprintf(stderr, "SHIFTING dmi. tdi: %ld\n", tdi);

                    // ignore first shift (dtmcontrol_scan_via_bscan() Note the starting offset is bit 1, not bit 0.  In BSCAN tunnel, there is a one-bit TCK skew between output and input)
                    tdo = 0;
                    if (shift_amount >= 1) {

                        // // DEBUG
                        // fprintf(stderr, "DMI Shift Register: ");
                        // print_dmi(dmi_shift_register);

                        // preload bit to send
                        tdo = dmi_shift_register & 0x01;

                        // shift data from the tdi register in
                        dmi_shift_register = dmi_shift_register >> 1;

                        // shift in tdi from the left
                        dmi_shift_register |= (tdi << (ABITS_LENGTH + (34 - 1))); // size of dmi container register is variable bits
                    }
                    shift_amount++;
                    break;

                default:
                    fprintf(stderr, "[Error] A Unknown instruction register!!! %d \n", instruction_container_register);
                    break;
                }

                break;

            // Shift in a bit from tdi into IR (on the rising edge) and also out from the IR to tdi (on the falling edge)
            case SHIFT_IR:
                //fprintf(stderr, "SHIFT_IR entered. rising_edge_clk: %d. \n", rising_edge_clk);
                //fprintf(stderr, "SHIFT_IR entered\n");

                // prepare send (which happens on the falling edge of the clock)
                // preload the pin with the value that is shifted out from the shift register
                tdo = instruction_shift_register & 0x01;

                // fprintf(stderr, "Before: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

                // shift data from the tdi register in
                instruction_shift_register = instruction_shift_register >> 1;
                instruction_shift_register |= (tdi << (5 - 1)); // size of ircode container register is 5 bits

                // fprintf(stderr, "After: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

            break;
        }
    }
}

void remote_bitbang_t::state_entered(tsm_state new_state, uint8_t rising_edge_clk)
{
    // fprintf(stderr, "state_entered\n");

    uint64_t dmi_address = 0x00;
    uint64_t dmi_data = 0x00;
    uint64_t dmi_op = 0x00;

    switch (new_state)
    {

    // In this state all test-modes (for example extest-mode) are reset, which will disable their operation, allowing the chip to follow its normal operation.
    case TEST_LOGIC_RESET:
        // fprintf(stderr, "TEST_LOGIC_RESET entered\n");

        // TODO: write IDCODE value into DR!
        // see: https://openocd.org/doc/pdf/openocd.pdf#page=69&zoom=100,120,96
        instruction_container_register = static_cast<uint8_t>(RiscV_DTM_Registers::JTAG_IDCODE);

        break;

    // This is the resting state during normal operation.
    case RUN_TEST_IDLE:
        // fprintf(stderr, "RUN_TEST_IDLE entered\n");
        break;

    // These are the starting states respectively for accessing one of the data registers
    // (the boundary-scan or bypass register in the minimal configuration) or the instruction register.
    case SELECT_DR_SCAN:
        // fprintf(stderr, "SELECT_DR_SCAN entered\n");

        // TODO: select the data register.
        //
        // The data register is a symbol register which means it points to another register.
        //
        // The real register that DR points to is specified via the IR register contents.
        // Real registers are indexed vai the current value of IR. Therefore first load some
        // value into IR then perform SELECT_DR_SCAN
        //
        // Remember: during TAP reset, IR is preloaded with the index of the IDCODE register!
        // (Or also possible the bypass register). This means that after TAP reset, when activating
        // The SELECT_DR_SCAN, the DR will point to the IDCODE register!
        //
        // Lets assume this simulated TAP enters IDCODE into IR on reset. This means during
        // SELECT_DR_SCAN, the IDCODE register is selected.
        //
        // The result of all this selecting real registers is that whenever
        break;
    case SELECT_IR_SCAN:
        // fprintf(stderr, "SELECT_IR_SCAN entered\n");
        break;

    // These capture the current value of one of the data registers or the instruction register respectively
    // into the scan cells (= Shift register). This is a slight misnomer for the instruction register, since it is usual
    // to capture status information, rather than the actual instruction with Capture-IR.
    case CAPTURE_DR:
        // fprintf(stderr, "CAPTURE_DR entered\n");

        // TODO: copy the current value stored inside the selected [data container register] into the corresponding
        // [data shift register].
        //
        // Real data registers always come in pairs. The pairs consist of a [data container register] and a
        // [data shift register].
        //
        // There is a real [data container register] that stores the current value.
        // Then there is the other element to the pair which is the [data shift register]
        //
        // The DR symbolic register points to the pair. When the DR symbolic register points
        // onto a pair, the shift register of that pair is installed right in between the
        // tdi and tdo ports. This means that the shift register will interact with the openocd
        // JTAG client.
        //
        // The IR register pair is special. There is no symbolic pointer register that points to IR.
        // All IR  state machines states implicitly operate on the IR register!
        //
        // This [data shift register] is the register that will get bit shifted in from
        // tdi on the rising edge of the clock when in SHIFT_DR or SHIFT_IR state.
        // The tdo value is taken on the falling edge of the clock from the shift register.
        //
        // There are operations to load a value from the [data container register] into the [data shift register].
        // This operation is performed in the states CAPTURE_DR for the symbolic DR register and CAPTURE_IR respectively
        // for the IR register.
        //
        // There is an operation to store the current value inside the data shift register back into the data container
        // register. This operation is performed in the state UPDATE_DR and in the state UPDATE_IR for the IR register pair.
        //
        // The overall idea is that the data container register remains as is during shift operations because the
        // shift operations only affect the data shift register. Once the client is happy with the data that they
        // have shifted into the system, the client can decide to commit that data to the data container register
        // by transitioning into the UPDATE_DR, UPDATE_IR states.
        //
        // In order to read from a register, the client has to first transition into the CAPTURE_DR or CAPTURE_IR
        // states so that the data is copied from the data container registers into the shift registers, because the
        // data that a client will receive from the system will always come from the data shift register and
        // never directly from the data container register.

        switch (static_cast<RiscV_DTM_Registers>(instruction_container_register))
        {

        case RiscV_DTM_Registers::JTAG_IDCODE:
            fprintf(stderr, "\nCAPTURE_DR - capturing IDCODE into id_code_shift_register\n");
            id_code_shift_register = id_code_container_register;
            shift_amount = 0;
            break;

            // TODO case for bypass

        case RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS:
            fprintf(stderr, "\nCAPTURE_DR - RISCV_DTM_REGISTERS::DTM_CONTROL_AND_STATUS - capturing dtmcs_container_register into dtmcs_shift_register - ");
            print_dtmcs(dtmcs_container_register);
            dtmcs_shift_register = dtmcs_container_register;
            shift_amount = 0;
            break;

        case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:
#ifdef OPENOCD_POLLING_DEBUG // openocd keeps polling the target every 400ms which results in massive spam
            fprintf(stderr, "\nCAPTURE_DR - RISCV_DTM_REGISTERS::DEBUG_MODULE_INTERFACE_ACCESS - capturing dmi_container_register into dmi_shift_register - ");
            print_dmi(dmi_container_register);
#endif
            dmi_shift_register = dmi_container_register;
            shift_amount = 0;
            break;

        default:
            fprintf(stderr, "[Error] B Unknown instruction register!!!\n");
            break;
        }

        break;
    case CAPTURE_IR:
        // fprintf(stderr, "CAPTURE_IR entered\n");

        // copy data from the IR container register into the IR shift register.
        // the length of the IR register can be specified via the openocd.cfg file.
        // It is set to 8 in this example.
        instruction_shift_register = static_cast<uint8_t>(instruction_container_register);
        break;

    // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO
    // (on the falling edge of TCK) from the currently selected data or instruction register respectively.
    case SHIFT_DR:
        // fprintf(stderr, "SHIFT_DR entered.\n");
        // the actual shifting is performed on the falling edge
        break;

    // Shift in a bit from tdi into IR (on the rising edge) and also out from the IR to tdi (on the falling edge)
    case SHIFT_IR:
        // fprintf(stderr, "SHIFT_IR entered. rising_edge_clk: %d. \n", rising_edge_clk);
        // the actual shifting is performed on the falling edge
        break;

    // These are the exit states for the corresponding shift state.
    // From here the state machine can either enter a pause state or enter the update state.
    case EXIT1_DR:
        // fprintf(stderr, "EXIT1_DR entered\n");
        break;
    case EXIT1_IR:
        // fprintf(stderr, "EXIT1_IR entered\n");
        break;

    // Pause in shifting data into the data or instruction register.
    // This allows for example test equipment supplying TDO to reload buffers etc.
    case PAUSE_DR:
        // fprintf(stderr, "PAUSE_DR entered\n");
        break;
    case PAUSE_IR:
        // fprintf(stderr, "PAUSE_IR entered\n");
        break;

    // These are the exit states for the corresponding pause state.
    // From here the state machine can either resume shifting or enter the update state.
    case EXIT2_DR:
        // fprintf(stderr, "EXIT2_DR entered\n");
        break;
    case EXIT2_IR:
        // fprintf(stderr, "EXIT2_IR entered\n");
        break;

    // The value shifted into the scan cells during the previous states is driven into the chip (from inputs) 
    // or onto the interconnect (for outputs).
    case UPDATE_DR:
        // fprintf(stderr, "UPDATE_DR entered\n");
        switch (static_cast<RiscV_DTM_Registers>(instruction_container_register))
        {

        case RiscV_DTM_Registers::JTAG_IDCODE:
            // fprintf(stderr, "UPDATE_DR RiscV_DTM_Registers::JTAG_IDCODE\n");
            id_code_container_register = id_code_shift_register;
            break;

        case RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS:
            // fprintf(stderr, "UPDATE_DR RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS\n");

            // print before the change
            print_dtmcs(dtmcs_container_register);

            // DEBUG just activate this line again!
            //dtmcs_container_register = dtmcs_shift_register;

            // print after the change
            print_dtmcs(dtmcs_container_register);
            break;

        // Debug Specification, page 95, 6.1.5 Debug Module Interface Access (Register)
        // Allows access to the Debug Module Interface (DMI) which is the endpoint of
        // the debug (JTAG) connection and is connected to one or more Debug Modules (DM).
        // 
        case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:

#ifdef OPENOCD_POLLING_DEBUG // openocd keeps polling the target every 400ms which results in massive spam
            fprintf(stderr, "\nUPDATE_DR RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS\n");

            // DEBUG print before the change
            print_dmi(dmi_container_register);
#endif

            // TODO I think this makes no sense
            if (dmi_shift_register != 0x00) {
                dmi_container_register = dmi_shift_register;

#ifdef OPENOCD_POLLING_DEBUG // openocd keeps polling the target every 400ms which results in massive spam
                // print after the change
                print_dmi(dmi_container_register);
#endif
            }

            dmi_address = get_dmi_address(dmi_container_register);
            dmi_data = get_dmi_data(dmi_container_register);
            dmi_op = get_dmi_op(dmi_container_register);

            // DEBUG
            //fprintf(stderr, "dmi_address: %ld, dmi_data: %ld, dmi_op: %ld (%s)\n", dmi_address, dmi_data, dmi_op, operation_as_string(dmi_op).c_str());

            // The user accesses the registers inside the DebugModule over the DebugBus which might be
            // AXI, AMBA, .... First the command to execute a read operation is sent to the DebugModuleInterface (DMI)
            // which then talks to the DebugModule (DM) over the bus to access a register.

            // 0x04 (Abstract Data 0 (data0))
            // 0x05 (Abstract Data 1 (data1))
            // ...
            // 0x0f (Abstract Data 11 (data11))
            if ((dmi_address >= 0x04) && (dmi_address <= 0x0f)) {
            
                // data 0 through data 11 (Registers data 0 - data 11) are registers that may
                // be read or changed by abstract commands. datacount indicates how many 
                // of them are implemented, starting at data0 counting up. 
                //
                // Table 2 shows how abstract commands use these registers.

                // // aampostincrement
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 0 (data0) (0x04) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                uint32_t idx = dmi_address - 0x04;

                if (dmi_op == 0x01) {

                    fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data %d (data%d) (0x%02x) READ. value = %ld\n", idx, idx, idx, abstract_data[dmi_address - 0x04]);
                    dmi_data = abstract_data[idx];

                    // success, the operation 0x00 used in a response is interpreted by openocd
                    // as a successfull termination of the requested operation
                    dmi_op = 0x00;

                    // set a value into the dmi_container_register
                    dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                        ((dmi_data & 0xFFFFFFFF) << 2) | 
                        ((dmi_op & 0b11) << 0);

                } else if (dmi_op == 0x02) {

                    fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data %d (data%d) (0x%02x) WRITE \n", idx, idx, idx);
                    abstract_data[dmi_address - 0x04] = dmi_data;

                }

                //fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

            }
            // 0x10 == DebugModule Control Register (DebugSpec, Page 26 and Page 30)
            else if (dmi_address == 0x10) {
                
                // read operation
                if ((dmi_address == 0x10) && (dmi_op == 0x01)) {

                    fprintf(stderr, "\nDebugModule Control Register READ\n");

                    // construct the response
                    uint64_t debug_module_control = 
                        (haltreq << 31) |           // Writing 0 clears the halt request bit for all currently selected harts.
                        (resumereq << 30) |         // Writing 1 causes the currently selected harts to resume once, if they are halted when the write occurs. 
                        (hartreset << 29) |
                        (ackhavereset << 28) |
                        (ackunavail << 27) |
                        (hasel << 26) |
                        (hartsello << 16) |
                        (hartselhi << 6) |
                        (setkeepalive << 5) |
                        (clrkeepalive << 4) |
                        (setresethaltreq << 3) |
                        (clrresethaltreq << 2) |
                        (ndmreset << 1) |
                        (dmactive << 0);

                    // success, the operation 0x00 used in a response is interpreted by openocd
                    // as a successfull termination of the requested operation
                    dmi_op = 0x00;

                    // set a value into the dmi_container_register
                    dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                        ((debug_module_control & 0xFFFFFFFF) << 2) | 
                        ((dmi_op & 0b11) << 0);

                    // // DEBUG
                    // fprintf(stderr, "Outgoing dmi_control_register after READ: ");
                    // print_dmi(dmi_container_register);

                }  
                // write operation
                else if ((dmi_address == 0x10) && (dmi_op == 0x02)) {

                    fprintf(stderr, "\nDebugModule Control Register WRITE\n");

                    // https://riscv.org/wp-content/uploads/2019/03/riscv-debug-release.pdf
                    
                    // parse the incoming fields
                    haltreq = ((dmi_data >> 31) & 0b1);             // Writing 0 clears the halt request bit for all currently selected harts.
                    resumereq = ((dmi_data >> 30) & 0b1);           // Writing 1 causes the currently selected harts to resume once, if they are halted when the write occurs. 
                    hartreset = ((dmi_data >> 29) & 0b1);           // This optional field writes the reset bit for all the currently selected harts. To perform a reset the debugger writes 1, and then writes 0 to deassert the reset signal.
                    ackhavereset = ((dmi_data >> 28) & 0b1);
                    ackunavail = ((dmi_data >> 27) & 0b1);
                    hasel = ((dmi_data >> 26) & 0b1);
                    hartsello = ((dmi_data >> 16) & 0b1111111111);
                    hartselhi = ((dmi_data >> 6) & 0b1111111111);
                    setkeepalive = ((dmi_data >> 5) & 0b1);
                    clrkeepalive = ((dmi_data >> 4) & 0b1);
                    setresethaltreq = ((dmi_data >> 3) & 0b1);
                    clrresethaltreq = ((dmi_data >> 2) & 0b1);
                    ndmreset = ((dmi_data >> 1) & 0b1);
                    dmactive = ((dmi_data >> 0) & 0b1);             // This bit serves as a reset signal for the Debug Module itself. 0 triggers a reset. 1 causes the module to remain as is without reset.

                    fprintf(stderr, "\n");
                    fprintf(stderr, "haltreq: %d\n", haltreq);
                    fprintf(stderr, "resumereq: %d\n", resumereq);
                    fprintf(stderr, "hartreset: %d\n", hartreset);
                    fprintf(stderr, "ackhavereset: %d\n", ackhavereset);
                    fprintf(stderr, "ackunavail: %d\n", ackunavail);
                    fprintf(stderr, "hasel: %d\n", hasel);
                    fprintf(stderr, "hartsello: %d\n", hartsello);
                    fprintf(stderr, "hartselhi: %d\n", hartselhi);
                    fprintf(stderr, "setkeepalive: %d\n", setkeepalive);
                    fprintf(stderr, "clrkeepalive: %d\n", clrkeepalive);
                    fprintf(stderr, "setresethaltreq: %d\n", setresethaltreq);
                    fprintf(stderr, "clrresethaltreq: %d\n", clrresethaltreq);
                    fprintf(stderr, "ndmreset: %d\n", ndmreset);
                    fprintf(stderr, "dmactive: %d\n", dmactive);

                    // single step requested
                    if (resumereq == 1) {

                        auto t = std::time(nullptr);
                        auto tm = *std::localtime(&t);

                        std::ostringstream oss;
                        oss << std::put_time(&tm, "%d-%m-%Y %H-%M-%S");
                        auto str = oss.str();

                        std::cout << str << std::endl;

                        fprintf(stderr, "\n %s [SINGLE_STEP] Selected harts perform single step requested!\n", str.c_str());

                        dpc += 4;
                    }

                    // dm restart requested by writing a 1 into the dmactive bit of the dmcontrol register
                    if (hasel == 0 && dmactive == 1) {

                        fprintf(stderr, "\nDM activate or remain active (not reset) requested!\n");

                        // simulated restart - seen openocd source code. riscv-013.c, line 1839, "Activating the DM."
                        // openocd writes a 1 into the DM's dmactive bit to tell the DM to activate.
                        // openocd then performs a wait loop in which the bit is read. When the dmactive bit is
                        // eventually read a 1, then openocd continues with the next step.
                        //
                        // The next step is to select a hart.
                        dmactive = 1;

                        // construct the response
                        uint64_t debug_module_control = 
                            (haltreq << 31) |
                            (resumereq << 30) |
                            (hartreset << 29) |
                            (ackhavereset << 28) |
                            (ackunavail << 27) |
                            (hasel << 26) |
                            (hartsello << 16) |
                            (hartselhi << 6) |
                            (setkeepalive << 5) |
                            (clrkeepalive << 4) |
                            (setresethaltreq << 3) |
                            (clrresethaltreq << 2) |
                            (ndmreset << 1) |
                            (dmactive << 0);

                        // success, the operation 0x00 used in a response is interpreted by openocd
                        // as a successfull termination of the requested operation
                        dmi_op = 0x00;

                        // set a value into the dmi_container_register
                        dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                            ((debug_module_control & 0xFFFFFFFF) << 2) | 
                            ((dmi_op & 0b11) << 0);

                        // // DEBUG
                        // fprintf(stderr, "Outgoing dmi_control_register after WRITE: ");
                        // print_dmi(dmi_container_register);
                    }
                    else if (hasel == 1) {

                        fprintf(stderr, "\nDM Hart Selection requested!\n");

                        // 0b111111111111111111111000001
                        //
                        // 1 - hasel
                        // 1111111111 - hartsello
                        // 1111111111 - hartselhi
                        // 0
                        // 0
                        // 0
                        // 0
                        // 0
                        // 1 - dmactive

                        // openocd does not know how many harts exist inside the DM.
                        // It will therefore perform a probe operation as outlined in the
                        // RISCV debug specification: page 30, 3.14.2 Debug Module Control (cmcontrol, 0x10)
                        // "A debugger should discover HARTSELLEN" by writing all ones to hartsel (assuming
                        // the maximum size) and reading back the value to see which bits were actually set"
                        //
                        // hartsel is a name for the combined high and low registers {hartsello, hartselhi}
                        //
                        // Every individual bit in hartsel stands for a hart. To check which harts exist,
                        // openocd writes a 1 into each bit and reads back the result. The RISCV processor
                        // will return the harts that have actually been selected, writing a 0 in bits for
                        // harts that do not even exist! That way openocd can discover which harts exist!
                        //
                        // Here a system with a single hart is simulated so only a single bit will be 
                        // return 1 (high), the others are set to 0 (low).

                        // debug module is active
                        dmactive = 1;

                        // only a single hart exists, set only the very first bit in hartsello
                        //hartsello = 1;
                        hartsello = 0;
                        hartselhi = 0;

                        // construct the response
                        uint64_t debug_module_control = 
                            (haltreq << 31) |
                            (resumereq << 30) |
                            (hartreset << 29) |
                            (ackhavereset << 28) |
                            (ackunavail << 27) |
                            (hasel << 26) |
                            (hartsello << 16) |
                            (hartselhi << 6) |
                            (setkeepalive << 5) |
                            (clrkeepalive << 4) |
                            (setresethaltreq << 3) |
                            (clrresethaltreq << 2) |
                            (ndmreset << 1) |
                            (dmactive << 0);

                        // success, the operation 0x00 used in a response is interpreted by openocd
                        // as a successfull termination of the requested operation
                        dmi_op = 0x00;

                        // set a value into the dmi_container_register
                        dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                            ((debug_module_control & 0xFFFFFFFF) << 2) | 
                            ((dmi_op & 0b11) << 0);

                    }

                }

            } 
            // 0x11 == DebugModule Status (dmstatus) (DebugSpec, Page 28) - 3.14.1 Debug Module Status
            else if (dmi_address == 0x11) {

#ifdef OPENOCD_POLLING_DEBUG // openocd keeps polling the target every 400ms which results in massive spam
                fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Status Register (0x11) \n");

                // read operation
                if (dmi_op == 0x01) {
                    fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Status Register (0x11) READ \n");
                } else if (dmi_op == 0x02) {
                    fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Status Register (0x11) WRITE \n");
                }
#endif

                uint32_t ndmresetpending = 0x00;
                uint32_t stickyunavail = 0x00;
                uint32_t impebreak = 0x00;
                uint32_t allhavereset = 0x00;
                uint32_t anyhavereset = 0x00;
                uint32_t allresumeack = 0x01; // this is checked when performing a single step by openocd (step) command
                uint32_t anyresumeack = 0x00;
                uint32_t allnonexistent = 0x00;
                uint32_t anynonexistent = 0x00;
                uint32_t allunavail = 0x00;
                uint32_t anyunavail = 0x00;
                uint32_t allrunning = 0x00;
                uint32_t anyrunning = 0x00;

                // set allhalted to true since this is a sensical way to make the openocd source code to return
                // an OK status for the method riscv013_get_hart_state() in src/target/riscv/riscv-013.c
                uint32_t allhalted = 0x01;
                uint32_t anyhalted = 0x00;

                // automatically authenticate the debugger as otherwise openocd goes into failure and outputs
                // this message: "Debugger is not authenticated to target Debug Module. (dmstatus=0x3). Use `riscv authdata_read` and `riscv authdata_write` commands to authenticate."
                uint32_t authenticated = 0x01;

                uint32_t authbusy = 0x00;
                uint32_t hasresethaltreq = 0x00;
                uint32_t confstrptrvalid = 0x00;

                // into version, enter either 2 or 3 since openocd will err out if not compatible version is returned
                // openocd for riscv supports the version constants 2 or 3
                // 2 stands for 0.13 and 3 stands for 1.0
                // see riscv_examine() in src/target/riscv/riscv.c in the openocd source code.
                uint32_t version = 0x03;
            
                // construct the response
                uint64_t debug_module_status = 
                    (ndmresetpending << 24) |
                    (stickyunavail << 23) |
                    (impebreak << 22) |
                    (allhavereset << 19) |
                    (anyhavereset << 18) |
                    (allresumeack << 17) |
                    (anyresumeack << 16) |
                    (allnonexistent << 15) |
                    (anynonexistent << 14) |
                    (allunavail << 13) |
                    (anyunavail << 12) |
                    (allrunning << 11) |
                    (anyrunning << 10) |
                    (allhalted << 9) |
                    (anyhalted << 8) |
                    (authenticated << 7) |
                    (authbusy << 6) |
                    (hasresethaltreq << 5) |
                    (confstrptrvalid << 4) |
                    (version << 0);

                //status_container_register = debug_module_status;

                // success, the operation 0x00 used in a response is interpreted by openocd
                // as a successfull termination of the requested operation
                dmi_op = 0x00;

                // set a value into the dmi_container_register
                dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                    ((debug_module_status & 0xFFFFFFFF) << 2) | 
                    ((dmi_op & 0b11) << 0);

                // after this, in the logs of openocd (log level -d4) there should be an output similar to this:
                // "Debug: 2755 50698 riscv-013.c:411 riscv_log_dmi_scan(): read: dmstatus=0x283 {version=1_0 authenticated=true allhalted=1}"
            
            } 
            // 3.14.6. Abstract Control and Status (abstractcs, at 0x16)
            else if (dmi_address == 0x16) {

                // read operation
                if (dmi_op == 0x01) {

                    // construct the response
                    uint32_t abstractcs_container_register = 
                        (progbufsize << 24) |
                        (busy << 12) |
                        (relaxedpriv << 11) |
                        (cmderr << 8) |
                        (datacount << 0);

                    // success, the operation 0x00 used in a response is interpreted by openocd
                    // as a successfull termination of the requested operation
                    dmi_op = 0x00;

                    // set a value into the dmi_container_register
                    dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                        ((abstractcs_container_register & 0xFFFFFFFF) << 2) | 
                        ((dmi_op & 0b11) << 0);

                    // // DEBUG
                    // fprintf(stderr, "Outgoing dmi_control_register after READ: ");
                    // print_dmi(dmi_container_register);

                // write operation
                } else if (dmi_op == 0x02) {

                    // Writing this register while an abstract command is executing causes cmderr to become 1 (busy) once
                    // the command completes (busy becomes 0).

                    // progbufsize
                    //progbufsize = 0x00;

                    // 0 (ready): There is no abstract command currently being executed.
                    // 1 (busy): An abstract command is currently being executed
                    //busy = 0x00;

                    // This optional bit controls whether program buffer and
                    // abstract memory accesses are performed with the exact
                    // and full set of permission checks that apply based on the
                    // current architectural state of the hart performing the
                    // access, or with a relaxed set of permission checks (e.g. PMP
                    // restrictions are ignored). The details of the latter are
                    // implementation-specific.
                    // 0 (full checks): Full permission checks apply.
                    // 1 (relaxed checks): Relaxed permission checks apply
                    //relaxedpriv = 0x00;

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
                    //cmderr = 0x00;

                    // Number of data registers that are implemented as part of
                    // the abstract command interface. Valid sizes are 1 — 12.
                    //datacount = 0x00;

                    abstractcs_container_register = ((progbufsize & 0b11111) << 24) | 
                        ((busy & 0b1) << 12) |
                        ((relaxedpriv & 0b1) << 11) |
                        ((cmderr & 0b111) << 8) |
                        ((datacount & 0b1111) << 0);

                }

            } 
            // 3.14.7. Abstract Command (command, at 0x17)
            else if (dmi_address == 0x17) {

                // file:///home/wbi/Downloads/riscv-debug-specification.pdf

                // Register 0x17 is first written to start an abstract command to read a register for example.
                // Register 0x16 is then polled to see if the command has terminated
                // the resulting value is then read from register 0x04 for 32 bit and from
                // register 0x04 and 0x05 for 64 bit.

                // cmdtype: 0, control: 3280904
                uint64_t cmdtype = ((dmi_data >> 24) & 0xFF);
                uint64_t control = ((dmi_data >> 0) & 0xFFFFFF);

                // read operation
                if (dmi_op == 0x01) {

                    //fprintf(stderr, "\nAbstract Command READ\n");

                    uint32_t regno = (control >> 0) & 0xFFFF;
                    uint32_t write = (control >> 16) & 0x01;
                    uint32_t transfer = (control >> 17) & 0x01;
                    uint32_t postexec = (control >> 18) & 0x01;
                    uint32_t aarpostincrement = (control >> 19) & 0x01;
                    uint32_t aarsize = (control >> 20) & 0b111;

                    // CSR_MISA register
                    if (regno == 0x301) {

                        // Register 0x17 is first written to start an abstract command to read a register for example.
                        // Register 0x16 is then polled to see if the command has terminated
                        // the resulting value is then read from register 0x04 for 32 bit and from
                        // register 0x04 and 0x05 for 64 bit.

                        // The misa CSR is a WARL read-write register reporting the ISA supported by the hart. 
                        // This register must be readable in any implementation, but a value of zero can be 
                        // returned to indicate the misa register has not been implemented, requiring that CPU 
                        // capabilities be determined through a separate non-standard mechanism.

                        // read operation
                        fprintf(stderr, "read CSR_MISA (0x301)\n");

                        //                   MXL   ZYXWVUTSRQPONMLKJIHGFEDCBA
                        abstract_data[0] = 0b01000000000000000000000100101000;
        
                    } else {

                        fprintf(stderr, "\n[ERROR] UNKNOWN REGISTER !!!!! RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS ACCESS REGISTER COMMAND read regno: %" PRIu32 " (0x%04x), ABI-Name: %s\n", regno, regno, riscv_register_as_string(regno).c_str());

                    }

                    // The type determines the overall functionality of this abstract command.
                    //uint32_t cmdtype = 0x00;

                    // This field is interpreted in a command-specific manner, described for each abstract command.
                    //uint32_t control = 0x00;

                // write operation
                } else if (dmi_op == 0x02) {

                    //fprintf(stderr, "\nAbstract Command WRITE\n");

                    // Writes to this register cause the corresponding abstract command to be executed.
                    //
                    // Writing this register while an abstract command is executing causes cmderr to 
                    // become 1 (busy) once the command completes (busy becomes 0).
                    //
                    // If cmderr is non-zero, writes to this register are ignored.
                    //
                    // cmderr inhibits starting a new command to accommodate debuggers that, for
                    // performance reasons, send several commands to be executed in a row without checking
                    // cmderr in between. They can safely do so and check cmderr at the end without worrying
                    // that one command failed but then a later command (which might have depended on the
                    // previous one succeeding) passed.

                    //cmderr = 0x01;
                    cmderr = 0x00;

                    // DEBUG
                    //fprintf(stderr, "\ncmdtype: %d, control: %d\n", cmdtype, control);

                    // determine which type of abstract command is executed
                    if (cmdtype == 0x00) {

                        // 3.7.1.1. Access Register, page 18
                        //fprintf(stderr, "\nACCESS REGISTER COMMAND\n");

                        uint32_t regno = (control >> 0) & 0xFFFF;
                        uint32_t write = (control >> 16) & 0x01;
                        uint32_t transfer = (control >> 17) & 0x01;
                        uint32_t postexec = (control >> 18) & 0x01;
                        uint32_t aarpostincrement = (control >> 19) & 0x01;
                        uint32_t aarsize = (control >> 20) & 0b111;

                        // Check if the request has specified the correct register size XLEN.
                        // If the sent XLEN does not match the real XLEN, the debug interface has
                        // to set cmderr to 0x02
                        //
                        // perform "separate non-standard mechanism" to determine XLEN (register size)
                        if (aarsize == 2) {
                            // 32 bit
                        } else if (aarsize == 3) {
                            // 64 bit

                            // output error, this system is 32 bit

                            // if any of these operations fail, cmderr is set 
                            // and none of the remaining steps are executed.

                            // if a command has unsupported options set or if bits that are
                            // defined as zero are not 0, then the DM must set cmderr to 2 (not supported)
                            cmderr = 0x02;

                            // // set a value into the dmi_container_register
                            // dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                            //     ((abstractcs_container_register & 0xFFFFFFFF) << 2) | 
                            //     ((dmi_op & 0b11) << 0);

                        } else if (aarsize == 4) {
                            // 128 bit

                            // output error, this system is 32 bit

                            // if any of these operations fail, cmderr is set 
                            // and none of the remaining steps are executed.

                            // if a command has unsupported options set or if bits that are
                            // defined as zero are not 0, then the DM must set cmderr to 2 (not supported)
                            cmderr = 0x02;
                        }

                        fprintf(stderr, "\nACCESS REGISTER COMMAND regno: %" PRIu32 " (0x%04x), ABI-Name: %s\n", regno, regno, riscv_register_as_string(regno).c_str());

                        // try for one of the registers in the register file. GDB will offset them by 0x1000.
                        uint32_t regno_without_offset = regno - 0x1000;
                        if ((regno_without_offset >= 0) && (regno_without_offset <= 31)) {

                            fprintf(stderr, "\nACCESS REGISTER COMMAND found register from the register file\n");

                            if (write == 0) {

                                fprintf(stderr, "reading %s\n", riscv_register_as_string(regno_without_offset).c_str());

                                abstract_data[0] = register_file[regno_without_offset];

                            } else if (write == 1) {

                                fprintf(stderr, "write dpc (0x07b1)\n");

                                fprintf(stderr, "write dpc (0x07b1) written control: 0x%08lx\n", control);

                            }

                        } else if (regno == 0x300) {

                            // CSR_MSTATUS register - Zicsr extension
                            //
                            // https://book.rvemu.app/hardware-components/03-csrs.html
                            //
                            // The status registers, mstatus for M-mode and sstatus for S-mode, 
                            // keep track of and control the CPU's current operating status.
                            //
                            // mstatus is allocated at 0x300 and sstatus is allocated at 0x100. 
                            // It means we can access status registers by 0x300 and 0x100.

                            // 3.1.6 Machine Status Registers (mstatus and mstatush)
                            // The mstatus register is an MXLEN-bit read/write register formatted as 
                            //shown in Figure 1.6 for RV32 and Figure 1.7 for RV64. The mstatus register 
                            // keeps track of and controls the hart’s current operating state.
                            //
                            // A restricted view of mstatus appears as the sstatus register in the S-level ISA.

                            // https://five-embeddev.com/quickref/csrs.html

                            // [31]     SD          - Extension Context - Read-only bit that summarizes whether either the FS, VS or XS fields signal the presence of some dirty state that will require saving extended user context to memory.
                            // [30-23]  WPRI        - Reserved - Writes Preserve Values, Reads Ignore Values (WPRI)
                            // [22]     TSR         - The TSR (Trap SRET) bit is a WARL field that supports intercepting the supervisor exception return instruction, SRET.
                            // [21]     TW          - The TW (Timeout Wait) bit is a WARL field that supports intercepting the WFI instruction.
                            // [20]     TVM         - The TVM (Trap Virtual Memory) bit is a WARL field that supports intercepting supervisor virtual-memory management operations.
                            // [19]     MXR         - The MXR (Make eXecutable Readable) bit modifies the privilege with which loads access virtual memory. 0 - Only loads from pages marked readable will succeed. 1 - Loads from pages marked either readable or executable will succeed.
                            // [18]     SUM         - The SUM (permit Supervisor User Memory access) bit modifies the privilege with which S-mode loads and stores access virtual memory. 0 - S-mode memory accesses to pages that are accessible by U-mode will fault. 1. - S-mode memory accesses to pages that are accessible by U-mode are permitted.
                            // [17]     MPRV        - Modify Privilege
                            // [16-15]  XS[1:0]     - The XS field encodes the status of additional user-mode extensions and associated state.
                            // [14-13]  FS[1:0]     - The FS field encodes the status of the floating-point unit state, including the floating-point registers f0–f31 and the CSRs fcsr, frm, and fflags.
                            // [12-11]  MPP[1:0]    - Machine Previous Privilege mode. Two-level stack
                            // [10-9]   VS[1:0]     - The VS field encodes the status of the vector extension state, including the vector registers v0–v31 and the CSRs vcsr, vxrm, vxsat, vstart, vl, vtype, and vlenb.
                            // [8]      SPP         - Supervisor Previous Privilege mode
                            // [7]      MPIE        - Machine Prior Interrupt Enable
                            // [6]      UBE         - Endianness Control - Control the endianness of memory accesses made from S-mode other than instruction fetches. (Instruction fetches are always little-endian). 0 - Little Endian. 1 - Big Endian.
                            // [5]      SPIE        - Supervisor Prior Interrupt Enable
                            // [4]      WPRI        - Reserved - Writes Preserve Values, Reads Ignore Values (WPRI)
                            // [3]      MIE         - Machine Interrupt Enable - Global Interupt Enable (in M-Mode) (M-Mode = Machine Mode = application has full access)
                            // [2]      WPRI        - Reserved - Writes Preserve Values, Reads Ignore Values (WPRI)
                            // [1]      SIE         - Supervisor Interrupt Enable - Global Interupt Enable (in S-Mode) (S-Mode = Supervisor Mode = application has limited access)
                            // [0]      WPRI        - Reserved - Writes Preserve Values, Reads Ignore Values (WPRI)

                        } else if (regno == 0x301) {

                            // CSR_MISA register - Zicsr extension
                            //
                            // https://book.rvemu.app/hardware-components/03-csrs.html
                            // https://five-embeddev.com/riscv-priv-isa-manual/Priv-v1.12/machine.html
                            //
                            // Register 0x17 is first written to start an abstract command to read a register for example.
                            // Register 0x16 is then polled to see if the command has terminated
                            //
                            // The resulting value is then read from register 0 (0x04) for 32 bit 
                            // and from register 0 (0x04) and 1 (0x05) for 64 bit.

                            if (write == 0) {

                                fprintf(stderr, "read CSR_MISA (0x301)\n");

                                //                   MXL   ZYXWVUTSRQPONMLKJIHGFEDCBA
                                abstract_data[0] = 0b01000000000000000000000100101000;

                            } else if (write == 1) {

                                fprintf(stderr, "write CSR_MISA (0x301)\n");
                            }
            
                        } else if (regno == 0x07b0) {

                            // 4.8.1 Debug Control and Status (dcsr, at 0x7b0)

                            // xdebugver [31-28]    0: There is no external debug support. 
                            //                      4: External debug support exists as it is described in this document. 
                            //                      15: There is external debug support, but it does not conform to any available version of this spec.
                            // 0         [27-16]
                            // ebreakm   [15]       0: ebreak instructions in M-mode behave as described in the Privileged Spec. 
                            //                      1: ebreak instructions in M-mode enter Debug Mode.
                            // 0         [14]
                            // ebreaks   [13]       0: ebreak instructions in S-mode behave as described in the Privileged Spec.
                            //                      1: ebreak instructions in S-mode enter Debug Mode.
                            // ebreaku   [12]       0: ebreak instructions in U-mode behave as described in the Privileged Spec.
                            //                      1: ebreak instructions in U-mode enter Debug Mode.
                            // stepie    [11]       0: Interrupts are disabled during single stepping.
                            //                      1: Interrupts are enabled during single stepping.
                            //                      Implementations may hard wire this bit to 0. In
                            //                      that case interrupt behavior can be emulated by
                            //                      the debugger.
                            //                      The debugger must not change the value of this
                            //                      bit while the hart is running.
                            // stopcount [10]       0: Increment counters as usual.
                            //                      1: Don’t increment any counters while in Debug
                            //                      Mode or on ebreak instructions that cause entry into Debug Mode.
                            //                      These counters include the cycle and instret CSRs.
                            //                      This is preferred for most debugging scenarios.
                            //                      An implementation may hardwire this bit to 0 or 1.
                            //                      Stop Counters.
                            // stoptime  [9]        0: Increment timers as usual.
                            //                      1: Don’t increment any hart-local timers while in Debug Mode.
                            //                      An implementation may hardwire this bit to 0 or 1.
                            //                      Stop timers.
                            // cause     [8-6]      Explains why Debug Mode was entered.
                            //                      When there are multiple reasons to enter Debug
                            //                      Mode in a single cycle, hardware should set cause
                            //                      to the cause with the highest priority.
                            //                      1: An ebreak instruction was executed. (priority 3)
                            //                      2: The Trigger Module caused a breakpoint exception. (priority 4, highest)
                            //                      3: The debugger requested entry to Debug Mode using haltreq. (priority 1)
                            //                      4: The hart single stepped because step was set. (priority 0, lowest)
                            //                      5: The hart halted directly out of reset due to resethaltreq. It is also acceptable to report 3 when
                            //                      this happens. (priority 2) 
                            //                      Other values are reserved for future use.
                            // 0         [5]  
                            // mprven    [4]        0: MPRV in mstatus is ignored in Debug Mode.
                            //                      1: MPRV in mstatus takes effect in Debug Mode.
                            //                      Implementing this bit is optional. It may be tied to either 0 or 1.
                            // nmip      [3]        When set, there is a Non-Maskable-Interrupt
                            //                      (NMI) pending for the hart.
                            //                      Since an NMI can indicate a hardware error condition, reliable debugging may no longer be possible
                            //                      once this bit becomes set. This is implementationdependent.
                            // step      [2]        When set and not in Debug Mode, the hart will only execute a single instruction and then enter Debug Mode. 
                            //                      If the instruction does not complete due to an exception, the hart will immediately enter Debug Mode before executing the trap
                            //                      handler, with appropriate exception registers set.
                            //                      The debugger must not change the value of this
                            //                      bit while the hart is running.
                            // prv       [1-0]      Contains the privilege level the hart was operating
                            //                      in when Debug Mode was entered. The encoding
                            //                      is described in Table 4.5. A debugger can change
                            //                      this value to change the hart’s privilege level when
                            //                      exiting Debug Mode.
                            //                      Not all privilege levels are supported on all harts.
                            //                      If the encoding written is not supported or the
                            //                      debugger is not allowed to change to it, the hart
                            //                      may change to any supported privilege level.

                            if (write == 0) {

                                fprintf(stderr, "read dcsr (0x07b0)\n");

                                uint32_t xdebugver = (control >> 28) & 0b1111;
                                uint32_t ebreakm = (control >> 15) & 0b1;
                                uint32_t ebreaks = (control >> 13) & 0b1;
                                uint32_t ebreaku = (control >> 12) & 0b1;
                                uint32_t stepie = (control >> 11) & 0b1;
                                uint32_t stopcount = (control >> 10) & 0b1;
                                uint32_t stoptime = (control >> 9) & 0b1;
                                uint32_t cause = (control >> 6) & 0b111;
                                uint32_t mprven = (control >> 4) & 0b1;
                                uint32_t nmip = (control >> 3) & 0b1;
                                uint32_t step = (control >> 2) & 0b1;
                                uint32_t prv = (control >> 0) & 0b11;

                                fprintf(stderr, "write dcsr (0x07b0) xdebugver: %d\n", xdebugver);
                                fprintf(stderr, "write dcsr (0x07b0) ebreakm: %d\n", ebreakm);
                                fprintf(stderr, "write dcsr (0x07b0) ebreaks: %d\n", ebreaks);
                                fprintf(stderr, "write dcsr (0x07b0) ebreaku: %d\n", ebreaku);
                                fprintf(stderr, "write dcsr (0x07b0) stepie: %d\n", stepie);
                                fprintf(stderr, "write dcsr (0x07b0) stopcount: %d\n", stopcount);
                                fprintf(stderr, "write dcsr (0x07b0) stoptime: %d\n", stoptime);
                                fprintf(stderr, "write dcsr (0x07b0) cause: %d\n", cause);
                                fprintf(stderr, "write dcsr (0x07b0) mprven: %d\n", mprven);
                                fprintf(stderr, "write dcsr (0x07b0) nmip: %d\n", nmip);
                                fprintf(stderr, "write dcsr (0x07b0) step: %d\n", step);
                                fprintf(stderr, "write dcsr (0x07b0) prv: %d\n", prv);

                            } else if (write == 1) {

                                fprintf(stderr, "write dcsr (0x07b0)\n");

                                uint32_t xdebugver = 0x04;
                                uint32_t ebreakm = 0x01;
                                uint32_t ebreaks = 0x01;
                                uint32_t ebreaku = 0x01;
                                uint32_t stepie = 0x00;
                                uint32_t stopcount = 0x01;
                                uint32_t stoptime = 0x00;
                                uint32_t cause = 0x00;
                                uint32_t mprven = 0x00;
                                uint32_t nmip = 0x00;
                                uint32_t step = 0x00;
                                uint32_t prv = 0x00;
                            }

                        } else if (regno == 0x07b1) {

                            // 4.8.2 Debug PC (dpc, at 0x7b1)
                            //
                            // Upon entry to debug mode, dpc is updated with the virtual address of 
                            // the next instruction to be executed. The behavior is described in more detail in Table 4.3.
                            //
                            // When resuming, the hart’s PC is updated to the virtual address stored in dpc. 
                            // A debugger may write dpc to change where the hart resumes.

                            if (write == 0) {

                                fprintf(stderr, "read dpc (0x07b1)\n");

                                abstract_data[0] = dpc;

                            } else if (write == 1) {

                                fprintf(stderr, "write dpc (0x07b1)\n");

                                fprintf(stderr, "write dpc (0x07b1) written control: 0x%08lx\n", control);

                            }
                        
                        } else {

                            fprintf(stderr, "\n[ERROR] Abstract Command (command, at 0x17) - ACCESS REGISTER COMMAND - UNKNOWN REGISTER !!!!! ACCESS REGISTER COMMAND write regno: %" PRIu32 " (0x%04x), ABI-Name: %s\n", regno, regno, riscv_register_as_string(regno).c_str());

                        }
                        
                    } else if (cmdtype == 0x01) {

                        // 3.7.1.2. Quick Access
                        fprintf(stderr, "\nQUICK_ACCESS\n");

                    } else if (cmdtype == 0x02) {

                        // 3.7.1.3. Access Memory, page 20
                        //fprintf(stderr, "\nACCESS_MEMORY_COMMAND\n");

                        // This table defines what registers are used for arg0, arg1 and arg2
                        //
                        // "Table 2 Use of Data Registers", DebugSpec, page 17
                        //
                        // Note: this table seems to be incorrect in the spec! OpenOCD uses the 64 bit
                        // row for 32 bit width! I'll tell mum...
                        //
                        // argument width | arg0 (return) | arg1         | arg2
                        // 32  (size==2)  | data0         | data1        | data2
                        // 64  (size==3)  | data0, data1  | data2, data3 | data4, data5
                        // 128 (size==4)  | data0+1+2+3   | data4+5+6+7  | data8+9+10+11

                        // before this code here is executed, the remote debugger has loaded:
                        // arg1 into the register 0x06 (Abstract Data 2 (data2))
                        // arg0 into the register 0x07 (Abstract Data 3 (data3))
                        //
                        // if this command is a write command, the requested semantics are
                        // to write the value stored inside Abstract Data 2 to the memory
                        // at the address stored in Abstract Data 3
                        //
                        // see Debug Spec, page 20 and page21

                        uint32_t aamvirtual = ((dmi_data >> 23) & 0b1);
                        uint32_t aamsize = ((dmi_data >> 20) & 0b111);
                        uint32_t aampostincrement = ((dmi_data >> 19) & 0b1);
                        uint32_t write = ((dmi_data >> 16) & 0b1);
                        uint32_t target_specific = ((dmi_data >> 14) & 0b11);

                        if (write) {

                            if (aamsize == 2) {

                                arg0 = abstract_data[0];
                                arg1 = abstract_data[1];

                            } else if (aamsize == 3) {

                                arg0 = abstract_data[0] << 32 | abstract_data[1];
                                arg1 = abstract_data[2] << 32 | abstract_data[3];

                            }

                            fprintf(stderr, "ACCESS_MEMORY_COMMAND +++ WRITE 0x%08lx -> 0x%08lx \n", arg0, arg1);

                        } else {

                            if (aamsize == 2) {

                                //arg0 = abstract_data[0];
                                arg1 = abstract_data[1];

                            } else if (aamsize == 3) {

                                //arg0 = abstract_data[0] << 32 | abstract_data[1];
                                arg1 = abstract_data[2] << 32 | abstract_data[3];

                            }

                            fprintf(stderr, "ACCESS_MEMORY_COMMAND +++ READ address: 0x%08lx \n", arg1);

                        }

                        // if aampostincrement is set, increment arg1
                        // arg1 for 32bit is: the data 1 register (0x05)
                        if (aampostincrement) {

                            // to implement correct auto-increment, write the next
                            // (incremented) address to dmi_data and from dmi_data
                            // into abstract_data[1]
                            dmi_data = arg1; 
                            dmi_data += (2 << (aamsize-1));

                        }

                        // to implement correct auto-increment (aampostincrement), write the next
                        // (incremented) address to dmi_data and from dmi_data
                        // into abstract_data[1]
                        //
                        // abstract_data[1] is then return when data1 (abstract_data[1])
                        // is read. When the external debugger retrieves an incremented
                        // address, it knows that the auto-increment (aampostincrement) feature
                        // is implemented
                        abstract_data[1] = dmi_data;
                        
                    }

                }
            
            } else {

                fprintf(stderr, "\nUPDATE_DR RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS -- [ERROR] UNKNOWN dmi_address!!! 0x%02lx (%s) \n", dmi_address, dm_register_as_string(dmi_address).c_str());

            }
            break;

        default:
            fprintf(stderr, "[Error] C Unknown instruction register!!!\n");
            break;
        }
        break;

    case UPDATE_IR:
        // fprintf(stderr, "UPDATE_IR entered\n");
        instruction_container_register = instruction_shift_register;
        break;

    default:
        fprintf(stderr, "[Error] Unknown state!!!\n");
        return;
    }
}

/// @brief Performs a single Request-Response socket iteration.
/// 
/// Each iteration comprises the following steps:
/// 1. Peform read from socket for openocd input
/// 2. Determine which command arrived and execute a specific handler
/// 3. If the handler wants to return output, write output into socket
/// 4. If the handler wants to quit the server, quit the server
void remote_bitbang_t::execute_command()
{
    // fprintf(stderr, "execute_command()\n");

    char command;
    int again = 1;
    while (again)
    {
        ssize_t num_read = read(client_fd, &command, sizeof(command));
        // fprintf(stderr, "num_read %ld\n", num_read);
        if (num_read == -1)
        {
            if (errno == EAGAIN)
            {
                // We'll try again the next call.
                // fprintf(stderr, "Received no command. Will try again on the next call\n");
                //fprintf(stderr, "Sleep 10ms\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            else
            {
                fprintf(stderr, "remote_bitbang failed to read on socket: %s (%d)\n",
                        strerror(errno), errno);
                again = 0;
                abort();
            }
        }
        else if (num_read == 0)
        {
            fprintf(stderr, "No command received\n");
            again = 1;

            fprintf(stderr, "sleep 1000\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        else
        {
            again = 0;
        }
    }

    // fprintf(stderr, "Received a command %c\n", command);
    //fprintf(stderr, "%c ", command);

    // B - Blink on
    // b - Blink off
    // R - Read request
    // Q - Quit request
    // 0 - Write 0 0 0
    // 1 - Write 0 0 1
    // 2 - Write 0 1 0
    // 3 - Write 0 1 1
    // 4 - Write 1 0 0
    // 5 - Write 1 0 1
    // 6 - Write 1 1 0
    // 7 - Write 1 1 1
    // r - Reset 0 0
    // s - Reset 0 1
    // t - Reset 1 0
    // u - Reset 1 1
    // O - SWDIO drive 1
    // o - SWDIO drive 0
    // c - SWDIO read request
    // d - SWD write 0 0
    // e - SWD write 0 1
    // f - SWD write 1 0
    // g - SWD write 1 1

    int dosend = 0;
    char tosend = '?';
    switch (command)
    {

    // B - Blink on
    case 'B':
        //fprintf(stderr, "*BLINK ON*\n");
        break;

    // b - Blink off
    case 'b': /* fprintf(stderr, "_______\n"); */
        //fprintf(stderr, "*BLINK off*\n");
        break;

    // r - Reset 0 0
    case 'r':
        // perform both tap and system reset (signals are active low, therefore 0 causes both resets)
        reset(0, 0);
        break; // This is wrong. 'r' has other bits that indicated TRST and SRST.

    // 0 - Write tck:0 tms:0 tdi:0
    case '0':
        set_pins(0, 0, 0);
        break;

    // 1 - Write tck:0 tms:0 tdi:1
    case '1':
        set_pins(0, 0, 1);
        break;

    // 2 - Write tck:0 tms:1 tdi:0
    case '2':
        set_pins(0, 1, 0);
        break;

    // 3 - Write tck:0 tms:1 tdi:1
    case '3':
        set_pins(0, 1, 1);
        break;

    // 4 - Write tck:1 tms:0 tdi:0
    case '4':
        set_pins(1, 0, 0);
        break;

    // 5 - Write tck:1 tms:0 tdi:1
    case '5':
        set_pins(1, 0, 1);
        break;

    // 6 - Write tck:1 tms:1 tdi:0
    case '6':
        set_pins(1, 1, 0);
        break;

    // 7 - Write tck:1 tms:1 tdi:1
    case '7':
        set_pins(1, 1, 1);
        break;

    // R - Read request
    case 'R':
        dosend = 1;
        tosend = tdo ? '1' : '0';
        break;

    // Q - Quit request
    case 'Q':
        quit = 1;
        break;

    default:
        fprintf(stderr, "remote_bitbang got unsupported command '%c'\n", command);
    }

    // this is where the server answers to the client
    if (dosend)
    {
        while (1)
        {

#ifdef OPENOCD_POLLING_DEBUG // openocd keeps polling the target every 400ms which results in massive spam
            // // 48d == 0x30 == '0', 49 == 0x31 == '1'
            // //fprintf(stderr, "Sending %d\n", tosend);
            if (tosend == 0x30) {
                fprintf(stderr, "0 ");
            } else {
                 fprintf(stderr, "1 ");
            }
#endif

            ssize_t bytes = write(client_fd, &tosend, sizeof(tosend));
            if (bytes == -1)
            {
                fprintf(stderr, "failed to write to socket: %s (%d)\n", strerror(errno), errno);
                abort();
            }
            if (bytes > 0)
            {
                break;
            }
        }
    }

    // quit the server
    if (quit)
    {
        // The remote disconnected.
        fprintf(stderr, "Remote end disconnected\n");
        close(client_fd);
        client_fd = 0;
    }
}

void remote_bitbang_t::print_dtmcs(uint32_t dtmcs) {

    uint8_t errinfo = ((dtmcs >> 0x12) & 0b111);
    uint8_t dtmhardreset = ((dtmcs >> 0x11) & 0b1);
    uint8_t dmireset = ((dtmcs >> 0x10) & 0b1);
    uint8_t idle = ((dtmcs >> 0x0C) & 0b111);
    uint8_t dmistat = ((dtmcs >> 0x0A) & 0b11);
    uint8_t abits = ((dtmcs >> 0x04) & 0b111111);
    uint8_t version = ((dtmcs >> 0x00) & 0b1111);

    fprintf(stderr, "DTMCS: [errinfo: 0x%02x][dtmhardreset: 0x%02x][dmireset: 0x%02x][idle: 0x%02x][dmistat: 0x%02x][abits: 0x%02x][version: 0x%02x]\n",
        errinfo, dtmhardreset, dmireset, idle, dmistat, abits, version);
}

/// @brief RISC-V Debug Specification, 1.0.0-rc3, page 93, section 6.1.4
/// @return default dtmcs value.
uint32_t remote_bitbang_t::init_dtmcs() {
    
    // 0 - (not implemented) not implemented
    // 1 - (dmi error) there was an error between DTM and DMI
    // 2 - (communication error) there was an error between DMI and the DMI subordinate
    // 3 - (device error) The DMI subordinate reported an error
    // 4 - (unknown) there is no error to report, or no further information available 
    //     about the error. This is the reset value if the field is implemented
    uint8_t errinfo = 0x00;

    // Writing 1 to this bit does a hard reset of the DTM, causing
    // the DTM to forget about any outstanding DMI
    // transactions, and returning all registers and internal state
    // to their reset value. In general this should only be used
    // when the Debugger has reason to expect that the
    // outstanding DMI transaction will never complete (e.g. a
    // reset condition caused an inflight DMI transaction to be
    // cancelled).
    uint8_t dtmhardreset = 0x00;

    // Writing 1 to this bit clears the sticky error state and resets
    // errinfo, but does not affect outstanding DMI transactions.
    uint8_t dmireset = 0x00;

    // This is a hint to the debugger of the minimum number of
    // cycles a debugger should spend in Run-Test/Idle after
    // every DMI scan to avoid a `busy' return code (dmistat of 3).
    // A debugger must still check dmistat when necessary.
    // 0: It is not necessary to enter Run-Test/Idle at all.
    // 1: Enter Run-Test/Idle and leave it immediately.
    // 2: Enter Run-Test/Idle and stay there for 1 cycle before
    // leaving.
    // And so on
    uint8_t idle = 0x00;

    // Read-only alias of op.
    uint8_t dmistat = 0x00;

    // The size of address in dmi. 
    uint8_t abits = ABITS_LENGTH;

    // 0 (0.11): Version described in spec version 0.11.
    // 1 (1.0): Version described in spec versions 0.13 and 1.0.
    // 15 (custom): Version not described in any available version
    // of this spec.
    uint8_t version = 0x01;

    uint32_t dtcms = 
        ((errinfo & 0b111) << 0x12) | 
        ((dtmhardreset & 0b1) << 0x11) | 
        ((dmireset & 0b1) << 0x10) |
        ((idle & 0b111) << 0x0C) |
        ((dmistat & 0b11) << 0x0A) |
        ((abits & 0b111111) << 0x04) | 
        ((version & 0b1111) << 0x00);

    print_dtmcs(dtcms);

    return dtcms;
}

void remote_bitbang_t::print_dmi(uint64_t dmi) {

    uint64_t address = ((dmi >> 34) & ABITS_MASK);
    uint64_t data = ((dmi >> 0x02) & 0xFFFFFFFF);
    uint64_t op = ((dmi >> 0x00) & 0b11);

    fprintf(stderr, "DMI: [address: 0x%02lx][data: 0x%08lx][op: 0x%02lx]\n",
        address, data, op);
}

uint64_t remote_bitbang_t::init_dmi() {

    // Address used for DMI access. In Update-DR this value is
    // used to access the DM over the DMI. op defines what this
    // register contains after every possible operation.
    uint64_t address = 0x10;

    // The data to send to the DM over the DMI during UpdateDR, and the data 
    // returned from the DM as a result of the previous operation.
    uint64_t data = 0x00000001;

    // When the debugger writes this field, it has the following meaning:
    //
    // 0 (nop): Ignore data and address. Don’t send anything over the DMI during Update-DR. This
    // operation should never result in a busy or error response.
    // The address and data reported in the following CaptureDR are undefined.
    // This operation leaves the values in address and data
    // UNSPECIFIED.
    //
    // 1 (read): Read from address. When this operation succeeds, address contains the
    // address that was read from, and data contains the value
    // that was read.
    //
    // 2 (write): Write data to address. This operation leaves the values in address and data
    // UNSPECIFIED.
    //
    // 3 (reserved): Reserved.
    //
    //
    //
    // When the debugger reads this field, it means the following:
    //
    // 0 (success): The previous operation completed successfully.
    //
    // 1 (reserved): Reserved.
    //
    // 2 (failed): A previous operation failed. The data scanned
    // into dmi in this access will be ignored. This status is sticky
    // and can be cleared by writing dmireset in dtmcs.
    // This indicates that the DM itself or the DMI responded
    // with an error. There are no specified cases in which the
    // DM would respond with an error, and DMI is not required
    // to support returning errors.
    // If a debugger sees this status, there might be additional
    // information in errinfo.
    //
    // 3 (busy): An operation was attempted while a DMI request
    // is still in progress. The data scanned into dmi in this
    // access will be ignored. This status is sticky and can be
    // cleared by writing dmireset in dtmcs. If a debugger sees
    // this status, it needs to give the target more TCK edges
    // between Update-DR and Capture-DR. The simplest way to
    // do that is to add extra transitions in Run-Test/Idle.
    uint64_t op = 0x00;

    uint64_t dmi = 
        ((address & ABITS_MASK) << 34) |
        ((data & 0xFFFFFFFF) << 0x02) | 
        ((op & 0b11) << 0x00);

    print_dmi(dmi);

    return dmi;
}

uint64_t remote_bitbang_t::get_dmi_address(uint64_t dmi) {
    uint64_t address = ((dmi >> 34) & ABITS_MASK);
    return address;
}

uint64_t remote_bitbang_t::get_dmi_data(uint64_t dmi) {
    uint64_t data = ((dmi >> 0x2) & 0xFFFFFFFF);
    return data;
}

uint64_t remote_bitbang_t::get_dmi_op(uint64_t dmi) {
    uint64_t op = ((dmi >> 0x00) & 0b11);
    return op;
}

std::string remote_bitbang_t::operation_as_string(uint64_t dmi_op) {
    switch (dmi_op) {

        case 0: 
            return std::string("NOP");

        case 1:
            return std::string("READ");

        case 2:
            return std::string("WRITE");

        default:
            return std::string("UNKNOWN");
    }
}

std::string remote_bitbang_t::dm_register_as_string(uint32_t address) {
    switch (address) {

        case 0x04: 
            return std::string("Abstract Data 0 (data0)");
        case 0x05:
            return std::string("Abstract Data 1 (data1)");
        case 0x06:
            return std::string("Abstract Data 2 (data2)");
        case 0x07:
            return std::string("Abstract Data 3 (data3)");
        case 0x08:
            return std::string("Abstract Data 4 (data4)");
        case 0x09:
            return std::string("Abstract Data 5 (data5)");
        case 0x0a:
            return std::string("Abstract Data 6 (data6)");
        case 0x0b:
            return std::string("Abstract Data 7 (data7)");
        case 0x0c:
            return std::string("Abstract Data 8 (data8)");
        case 0x0d:
            return std::string("Abstract Data 9 (data9)");
        case 0x0e:
            return std::string("Abstract Data 10 (data10)");
        case 0x0f:
            return std::string("Abstract Data 11 (data11)");

        case 0x10:
            return std::string("Debug Module Control (dmcontrol)");
        case 0x11:
            return std::string("Debug Module Status (dmstatus)");
        case 0x12:
            return std::string("Hart Info (hartinfo)");
        case 0x13:
            return std::string("Halt Summary 1 (haltsum1)");
        case 0x14:
            return std::string("Hart Array Window Select (hawindowsel)");
        case 0x15:
            return std::string("Hart Array Window (hawindow)");
        case 0x16:
            return std::string("Abstract Control and Status (abstractcs)");
        case 0x17:
            return std::string("Abstract Command (command)");
        case 0x18:
            return std::string("Abstract Command Autoexec (abstractauto)");
        case 0x19:
            return std::string("Configuration Structure Pointer 0 (confstrptr0)");
        case 0x1a:
            return std::string("Configuration Structure Pointer 1 (confstrptr1)");
        case 0x1b:
            return std::string("Configuration Structure Pointer 2 (confstrptr2)");
        case 0x1c:
            return std::string("Configuration Structure Pointer 3 (confstrptr3)");
        case 0x1d:
            return std::string("Next Debug Module (nextdm)");
        //case 0x1e:
        //    return std::string("");
        case 0x1f:
            return std::string("Custom Features (custom)");

        case 0x20:
            return std::string("Program Buffer 0 (progbuf0)");
        case 0x21:
            return std::string("Program Buffer 1 (progbuf1)");
        case 0x22:
            return std::string("Program Buffer 2 (progbuf2)");
        case 0x23:
            return std::string("Program Buffer 3 (progbuf3)");
        case 0x24:
            return std::string("Program Buffer 4 (progbuf4)");
        case 0x25:
            return std::string("Program Buffer 5 (progbuf5)");
        case 0x26:
            return std::string("Program Buffer 6 (progbuf6)");
        case 0x27:
            return std::string("Program Buffer 7 (progbuf7)");
        case 0x28:
            return std::string("Program Buffer 8 (progbuf8)");
        case 0x29:
            return std::string("Program Buffer 9 (progbuf9)");
        case 0x2a:
            return std::string("Program Buffer 10 (progbuf10)");
        case 0x2b:
            return std::string("Program Buffer 11 (progbuf11)");
        case 0x2c:
            return std::string("Program Buffer 12 (progbuf12)");
        case 0x2d:
            return std::string("Program Buffer 13 (progbuf13)");
        case 0x2e:
            return std::string("Program Buffer 14 (progbuf14)");
        case 0x2f:
            return std::string("Program Buffer 15 (progbuf15)");

        case 0x30:
            return std::string("Authentication Data (authdata)");
        // case 0x31:
        //     return std::string("");
        case 0x32:
            return std::string("Debug Module Control and Status 2 (dmcs2)");
        // case 0x33:
        //     return std::string("");
        case 0x34:
            return std::string("Halt Summary 2 (haltsum2)");
        case 0x35:
            return std::string("Halt Summary 3 (haltsum3)");
        // case 0x36:
        //     return std::string("");
        case 0x37:
            return std::string("System Bus Address 127:96 (sbaddress3)");
        case 0x38:
            return std::string("System Bus Access Control and Status (sbcs)");
        case 0x39:
            return std::string("System Bus Address 31:0 (sbaddress0)");
        case 0x3a:
            return std::string("System Bus Address 63:32 (sbaddress1)");
        case 0x3b:
            return std::string("System Bus Address 95:64 (sbaddress2)");
        case 0x3c:
            return std::string("System Bus Data 31:0 (sbdata0)");
        case 0x3d:
            return std::string("System Bus Data 63:32 (sbdata1)");
        case 0x3e:
            return std::string("System Bus Data 95:64 (sbdata2)");
        case 0x3f:
            return std::string("System Bus Data 127:96 (sbdata3)");

        case 0x40:
            return std::string("Halt Summary 0 (haltsum0)");

        case 0x70:
            return std::string("Custom Feature 0 (custom0)");
        case 0x71:
             return std::string("Custom Feature 1 (custom1)");
        case 0x72:
            return std::string("Custom Feature 2 (custom2)");
        case 0x73:
             return std::string("Custom Feature 3 (custom3)");
        case 0x74:
            return std::string("Custom Feature 4 (custom4)");
        case 0x75:
            return std::string("Custom Feature 5 (custom5)");
        case 0x76:
             return std::string("Custom Feature 6 (custom6)");
        case 0x77:
            return std::string("Custom Feature 7 (custom7)");
        case 0x78:
            return std::string("Custom Feature 8 (custom8)");
        case 0x79:
            return std::string("Custom Feature 9 (custom9)");
        case 0x7a:
            return std::string("Custom Feature 10 (custom10)");
        case 0x7b:
            return std::string("Custom Feature 11 (custom11)");
        case 0x7c:
            return std::string("Custom Feature 12 (custom12)");
        case 0x7d:
            return std::string("Custom Feature 13 (custom13)");
        case 0x7e:
            return std::string("Custom Feature 14 (custom14)");
        case 0x7f:
            return std::string("Custom Feature 15 (custom15)");

        default:
            return std::string("UNKNOWN");
    }
}

std::string remote_bitbang_t::riscv_register_as_string(uint32_t register_index) {

    // check riscv-openocd source code:
    //
    // src/target/riscv/riscv-013.c
    // uint32_t riscv013_access_register_command(struct target *target, uint32_t number, unsigned size, uint32_t flags)
    //
    // subtract the offset of 0x1000 that openocd adds when registers are requested with a value larger than or equal
    // t0 0x1000. I do not know what the reasoning behind this offset is yet!
    if (register_index >= 0x1000) {
        register_index -= 0x1000;
    }

    switch (register_index) {

        case 0: return "zero";
        case 1: return "ra";
        case 2: return "sp";
        case 3: return "gp";
        case 4: return "tp";
        case 5: return "t0";
        case 6: return "t1";
        case 7: return "t2";
        case 8: return "s0/fp";
        case 9: return "s1";
        case 10: return "a0";
        case 11: return "a1";
        case 12: return "a2";
        case 13: return "a3";
        case 14: return "a4";
        case 15: return "a5";
        case 16: return "a6";
        case 17: return "a7";
        case 18: return "s2";
        case 19: return "s3";
        case 20: return "s4";
        case 21: return "s5";
        case 22: return "s6";
        case 23: return "s7";
        case 24: return "s8";
        case 25: return "s9";
        case 26: return "s10";
        case 27: return "s11";
        case 28: return "t3";
        case 29: return "t4";
        case 30: return "t5";
        case 31: return "t6";

        // Privileged Machine CSR addresses.
        // #define CSR_MSTATUS 0x300
        case 0x0300: return "CSR_MSTATUS 0x0300 (Machine Mode Status Register, ZICSR extension)";

        // Privileged Machine CSR addresses.
        // #define CSR_MISA 0x301
        //
        // Found in riscv-gdb source code: gdb/include/opcode/riscv-opc.h
        case 0x0301: return "CSR_MISA 0x301 - Machine ISA register (misa) (Machine Mode Status Register, ZICSR extension)";

        // https://drive.google.com/file/d/1joBC2hWGEHJL4tFabjcqMpRqQNkqJ5WR/view
        // RISC-V Advanced Interrupt Architecture V1.0
        case 0x035c: return "0x035c Machine top external interrupt (only with an IMSIC) (mtopei)";

        // Number Privilege Width Name Description

        // Machine-Level Window to Indirectly Accessed Registers
        // 0x350 MRW XLEN miselect Machine indirect register select
        // 0x351 MRW XLEN mireg Machine indirect register alias

        // Machine-Level Interrupts

        // 0x304 MRW 64 mie Machine interrupt-enable bits
        // 0x344 MRW 64 mip Machine interrupt-pending bits
        // 0x35C MRW MXLEN mtopei Machine top external interrupt (only with an

        // IMSIC)

        // 0xFB0 MRO MXLEN mtopi Machine top interrupt
        // Delegated and Virtual Interrupts for Supervisor Level
        // 0x303 MRW 64 mideleg Machine interrupt delegation
        // 0x308 MRW 64 mvien Machine virtual interrupt enables
        // 0x309 MRW 64 mvip Machine virtual interrupt-pending bits

        // Machine-Level High-Half CSRs (RV32 only)

        // 0x313 MRW 32 midelegh Upper 32 bits of of mideleg (only with S-mode)
        // 0x314 MRW 32 mieh Upper 32 bits of mie
        // 0x318 MRW 32 mvienh Upper 32 bits of mvien (only with S-mode)
        // 0x319 MRW 32 mviph Upper 32 bits of mvip (only with S-mode)
        // 0x354 MRW 32 miph Upper 32 bits of mip

        

        // https://riscv.org/wp-content/uploads/2019/03/riscv-debug-release.pdf
        // 5.2.2 Trigger Data 1 (tdata1, at 0x7a1) . . . . . . . . . . . . . . . . . . . . . . . . 50
        // 5.2.3 Trigger Data 2 (tdata2, at 0x7a2) . . . . . . . . . . . . . . . . . . . . . . . . 50
        // 5.2.4 Trigger Data 3 (tdata3, at 0x7a3) . . . . . . . . . . . . . . . . . . . . . . . . 51
        // 5.2.5 Trigger Info (tinfo, at 0x7a4) . . . . . . . . . . . . . . . . . . . . . . . . . . 51
        // 5.2.6 Trigger Control (tcontrol, at 0x7a5) . . . . . . . . . . . . . . . . . . . . . . 51
        // 5.2.7 Machine Context (mcontext, at 0x7a8) . . . . . . . . . . . . . . . . . . . . . 52
        // 5.2.8 Supervisor Context (scontext, at 0x7aa) . . . . . . . . . . . . . . . . . . . . 52
        // 5.2.9 Match Control (mcontrol, at 0x7a1) . . . . . . . . . . . . . . . . . . . . . . . 53
        // 5.2.10 Instruction Count (icount, at 0x7a1) . . . . . . . . . . . . . . . . . . . . . . 58
        // 5.2.11 Interrupt Trigger (itrigger, at 0x7a1) . . . . . . . . . . . . . . . . . . . . . 59
        // 5.2.12 Exception Trigger (etrigger, at 0x7a1) . . . . . . . . . . . . . . . . . . . . . 60
        // 5.2.13 Trigger Extra (RV32) (textra32, at 0x7a3) . . . . . . . . . . . . . . . . . . . 60
        // 5.2.14 Trigger Extra (RV64) (textra64, at 0x7a3) . . . . . . . . . . . . . . . . . . . 61

        case 0x07b0: return "Debug Control and Status (dcsr, at 0x7b0)";
        case 0x07b1: return "Debug PC (dpc, at 0x7b1)";
        case 0x07b2: return "Debug Scratch Register 0 (dscratch0, at 0x7b2)";
        case 0x07b3: return "Debug Scratch Register 1 (dscratch1, at 0x7b3)";

        // Found in riscv-gdb (gdb/include/opcode/riscv-opc.h)
        // /* Unprivileged Vector CSR addresses.  */
        // #define CSR_VSTART 0x008
        // #define CSR_VXSAT 0x009
        // #define CSR_VXRM 0x00a
        // #define CSR_VCSR 0x00f
        // #define CSR_VL 0xc20
        // #define CSR_VTYPE 0xc21
        // #define CSR_VLENB 0xc22
        // 3106d = 0x0C22
        case 0x0c22: return "SR_VLENB 0xc22";

        // RISC-V Advanced Interrupt Architecture	June 2023	Smaia, Ssaia
        // https://wiki.riscv.org/display/HOME/Ratified+Extensions
        //
        // Machine Level CSRs:
        // 
        case 0x0fb0: return "0x0fb0 - Machine Top Interrupt (mtopi)";

        // 4104d == 0x1008
        // Debug: 153 2980 riscv-013.c:703 riscv013_execute_abstract_command(): [riscv.cpu0] access register=0x321008 {regno=0x1008 write=arg0 transfer=enabled postexec=disabled aarpostincrement=disabled aarsize=64bit}
        case 0x1008: return "s0/fp + gdb offset of 0x1000. I do not know why!";

        default: return "UNKNOWN";

    }

}
