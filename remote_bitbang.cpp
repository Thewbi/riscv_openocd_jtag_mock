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
        fprintf(stderr, "remote_bitbang failed to make socket: %s (%d)\n",
                strerror(errno), errno);
        abort();
    }
    fcntl(socket_fd, F_SETFL, O_NONBLOCK);
    int reuseaddr = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
                   sizeof(int)) == -1)
    {
        fprintf(stderr, "remote_bitbang failed setsockopt: %s (%d)\n",
                strerror(errno), errno);
        abort();
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        fprintf(stderr, "remote_bitbang failed to bind socket: %s (%d)\n",
                strerror(errno), errno);
        abort();
    }
    if (listen(socket_fd, 1) == -1)
    {
        fprintf(stderr, "remote_bitbang failed to listen on socket: %s (%d)\n",
                strerror(errno), errno);
        abort();
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(socket_fd, (struct sockaddr *)&addr, &addrlen) == -1)
    {
        fprintf(stderr, "remote_bitbang getsockname failed: %s (%d)\n",
                strerror(errno), errno);
        abort();
    }
    tck = 1;
    tms = 1;
    tdi = 1;
    trstn = 1;
    quit = 0;
    fprintf(stderr, "This emulator compiled with JTAG Remote Bitbang client. To enable, use +jtag_rbb_enable=1.\n");
    fprintf(stderr, "Listening on port %d\n",
            ntohs(addr.sin_port));
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
                // No client waiting to connect right now.
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            else
            {
                fprintf(stderr, "failed to accept on socket: %s (%d)\n", strerror(errno),
                        errno);
                again = 0;
                abort();
            }
        }
        else
        {
            fcntl(client_fd, F_SETFL, O_NONBLOCK);
            fprintf(stderr, "Accepted successfully\n");
            again = 0;
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
        // should the client send the 'R' command, it wants to read the current value of the tdo variable

        // Currently do not use the value that the driver (main()) program supplies since it currently is not
        // connected to any functioning logic at all.
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
        // Allows access to the Debug Module Interface (DMI) which terminates the debug (JTAG) connection
        // and is connected to one or more Debug Modules (DM).
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
            if (dmi_address == 0x04) {

                // data 0 through data 11 (Registers data 0 - data 11) are registers that may
                // be read or changed by abstract commands. datacount indicates how many 
                // of them are implemented, starting at data0 counting up. 
                //
                // Table 2 shows how abstract commands use these registers.

                // // aampostincrement
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 0 (data0) (0x04) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 0 (data0) (0x04) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 0 (data0) (0x04) WRITE \n");
                // }

                //fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                abstract_data_0 = dmi_data;
            
            } 
            // 0x05 (Abstract Data 1 (data1))
            else if (dmi_address == 0x05) {

                // data 0 through data 11 (Registers data 0 - data 11) are registers that may
                // be read or changed by abstract commands. datacount indicates how many 
                // of them are implemented, starting at data0 counting up. 
                //
                // Table 2 shows how abstract commands use these registers.

                // // aampostincrement
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 1 (data1) (0x05) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 1 (data1) (0x05) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 1 (data1) (0x05) WRITE \n");
                // }

                //fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                abstract_data_1 = dmi_data;
            
            }
            // 0x06 (Abstract Data 2 (data2)) 
            else if (dmi_address == 0x06) {

                // // aampostincrement
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 2 (data2) (0x06) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 2 (data2) (0x06) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 2 (data2) (0x06) WRITE \n");
                // }

                //fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                abstract_data_2 = dmi_data;
            }
            // 0x07 (Abstract Data 3 (data3))
            else if (dmi_address == 0x07) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 3 (data3) (0x07) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 3 (data3) (0x07) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 3 (data3) (0x07) WRITE \n");
                // }

                abstract_data_3 = dmi_data;
            }

            // 0x08 (Abstract Data 4 (data4))
            else if (dmi_address == 0x08) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 4 (data4) (0x08) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 4 (data4) (0x08) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 4 (data4) (0x08) WRITE \n");
                // }

                abstract_data_4 = dmi_data;
            }

            // 0x09 (Abstract Data 5 (data5))
            else if (dmi_address == 0x09) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 5 (data5) (0x09) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 5 (data5) (0x09) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 5 (data5) (0x09) WRITE \n");
                // }

                abstract_data_5 = dmi_data;
            }

            // 0x0a (Abstract Data 6 (data6))
            else if (dmi_address == 0x0a) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 6 (data6) (0x0a) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 6 (data6) (0x0a) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 6 (data6) (0x0a) WRITE \n");
                // }

                abstract_data_6 = dmi_data;
            }

            // 0x0b (Abstract Data 7 (data7))
            else if (dmi_address == 0x0b) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 7 (data7) (0x0b) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 7 (data7) (0x0b) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 7 (data7) (0x0b) WRITE \n");
                // }

                abstract_data_7 = dmi_data;
            }

            // 0x0c (Abstract Data 8 (data8))
            else if (dmi_address == 0x0c) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 8 (data8) (0x0c) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 8 (data8) (0x0c) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 8 (data8) (0x0c) WRITE \n");
                // }

                abstract_data_8 = dmi_data;
            }

            // 0x0d (Abstract Data 9 (data9))
            else if (dmi_address == 0x0d) {
                
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 9 (data9) (0x0d) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 9 (data9) (0x0d) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 9 (data9) (0x0d) WRITE \n");
                // }

                abstract_data_9 = dmi_data;
            }

            // 0x0e (Abstract Data 10 (data10))
            else if (dmi_address == 0x0e) {
                
                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 10 (data10) (0x0e) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 10 (data10) (0x0e) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 10 (data10) (0x0e) WRITE \n");
                // }

                abstract_data_10 = dmi_data;
            }

            // 0x0f (Abstract Data 11 (data11))
            else if (dmi_address == 0x0f) {

                // fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 11 (data11) (0x0f) \n");
                // fprintf(stderr, "\ndmi_data: %ld\n", dmi_data);

                // if (dmi_op == 0x01) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 11 (data11) (0x0f) READ \n");
                // } else if (dmi_op == 0x02) {
                //     fprintf(stderr, "\n~~~~~~~~ DebugModule (DM) Abstract Data 11 (data11) (0x0f) WRITE \n");
                // }

                abstract_data_11 = dmi_data;
            }

            // 0x10 == DebugModule Control Register (DebugSpec, Page 26 and Page 30)
            else if (dmi_address == 0x10) {
                
                // read operation
                if ((dmi_address == 0x10) && (dmi_op == 0x01)) {

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
                    // fprintf(stderr, "Outgoing dmi_control_register after READ: ");
                    // print_dmi(dmi_container_register);

                }  
                // write operation
                else if ((dmi_address == 0x10) && (dmi_op == 0x02)) {
                    
                    // parse the incoming fields
                    haltreq = ((dmi_data >> 31) & 0b1);
                    resumereq = ((dmi_data >> 30) & 0b1);
                    hartreset = ((dmi_data >> 29) & 0b1);
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
                    dmactive = ((dmi_data >> 0) & 0b1);

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

                    // dm restart requested by writing a 1 into the dmactive bit of the dmcontrol register
                    if (hasel == 0 && dmactive == 1) {

                        fprintf(stderr, "\nDM restart requested!\n");

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
            // 0x11 == DebugModule Status (DebugSpec, Page 28) - 3.14.1 Debug Module Status
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
                uint32_t allresumeack = 0x00;
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

                // Register 0x17 is first written to start an abstract command to read a register for example.
                // Register 0x16 is then polled to see if the command has terminated
                // the resulting value is then read from register 0x04 for 32 bit and from
                // register 0x04 and 0x05 for 64 bit.

                // read operation
                if (dmi_op == 0x01) {

                    //fprintf(stderr, "\nAbstract Command READ\n");

                    // The type determines the overall functionality of this abstract command.
                    uint32_t cmdtype = 0x00;

                    // This field is interpreted in a command-specific manner, described for each abstract command.
                    uint32_t control = 0x00;

                // write operation
                } else if (dmi_op == 0x02) {

                    //fprintf(stderr, "\nAbstract Command WRITE\n");

                    // Writes to this register cause the corresponding abstract command to be executed.
                    //
                    // Writing this register while an abstract command is executing causes cmderr to become 1 (busy) once
                    // the command completes (busy becomes 0).
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
                    
                    // cmdtype: 0, control: 3280904
                    uint32_t cmdtype = ((dmi_data >> 24) & 0xFF);
                    uint32_t control = ((dmi_data >> 0) & 0xFFFFFF);

                    //fprintf(stderr, "\ncmdtype: %d, control: %d\n", cmdtype, control);

                    if (cmdtype == 0x00) {

                        // 3.7.1.1. Access Register, page 18
                        //fprintf(stderr, "\nACCESS REGISTER COMMAND\n");

                        uint32_t regno = (control >> 0) & 0xFF;
                        uint32_t write = (control >> 16) & 0x01;
                        uint32_t transfer = (control >> 17) & 0x01;
                        uint32_t postexec = (control >> 18) & 0x01;
                        uint32_t aarpostincrement = (control >> 19) & 0x01;
                        uint32_t aarsize = (control >> 20) & 0b111;

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

                        //if ((regno != 0x01) && (regno != 0x08)) {
                        //    fprintf(stderr, "\nACCESS REGISTER COMMAND regno:%d\n", regno);
                        //}

                        fprintf(stderr, "\nACCESS REGISTER COMMAND regno: %d, ABI-Name: %s\n", regno, riscv_register_as_string(regno).c_str());

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
                                arg0 = abstract_data_0;
                                arg1 = abstract_data_1;

                                // arg0 = abstract_data_1 << 32 | abstract_data_0;
                                // arg1 = abstract_data_3 << 32 | abstract_data_2;
                            }

                            if (aamsize == 3) {
                                arg0 = abstract_data_0 << 32 | abstract_data_1;
                                arg1 = abstract_data_2 << 32 | abstract_data_3;
                            }

                            fprintf(stderr, "\nACCESS_MEMORY_COMMAND +++ WRITE 0x%08lx -> 0x%08lx \n", arg0, arg1);
                        } else {
                            fprintf(stderr, "\nACCESS_MEMORY_COMMAND +++ READ \n");
                        }

                        // if (aampostincrement) {
                        //     uint64_t increment = (2^(aamsize+3));
                        //     abstract_data_2 += increment;
                        //     abstract_data_3 += increment;
                        // }

                        // // set a value into the dmi_container_register
                        // dmi_container_register = ((dmi_address & ABITS_MASK) << 34) | 
                        //     ((debug_module_status & 0xFFFFFFFF) << 2) | 
                        //     ((dmi_op & 0b11) << 0);
                        
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
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

    switch (register_index) {

        case 0: return "zero";
        case 1: return "ra";
        case 2: return "sp";
        case 3: return "gp";
        case 4: return "tp";
        case 5: return "t0";
        case 6: return "t1";
        case 7: return "t2";
        case 8: return "fp";
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

        default: return "UNKNOWN";

    }

}
