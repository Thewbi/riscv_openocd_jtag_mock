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

                case RiscV_DTM_Registers::JTAG_IDCODE:

                    //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. SHIFT ", rising_edge_clk);

                    // prepare send (which happens on the falling edge of the clock)
                    // preload the pin with the value that is shifted out from the shift register
                    tdo = id_code_shift_register & 0x01;

                    // shift data from the tdi register in
                    id_code_shift_register = id_code_shift_register >> 1;

                    // shift in tdi from the left
                    id_code_shift_register |= (tdi << (32 - 1)); // size of idcode container register is 32 bits

                    break;

                // case Instruction::BYPASS:
                //     fprintf(stderr, "[Error] Unknown instruction register!!! BYPASS\n");
                //     break;

                case RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS:
                    // fprintf(stderr, "SHIFTING dtcms\n");

                    // ignore first shift (dtmcontrol_scan_via_bscan()Note the starting offset is bit 1, not bit 0.  In BSCAN tunnel, there is a one-bit TCK skew between output and input)
                    if (!first_shift) {

                        // preload bit to send
                        tdo = dtmcs_shift_register & 0x01;

                        // shift data from the tdi register in
                        dtmcs_shift_register = dtmcs_shift_register >> 1;

                        // shift in tdi from the left
                        dtmcs_shift_register |= (tdi << (32 - 1)); // size of dtmcs container register is 32 bits
                    }
                    first_shift = 0;
                    break;

                case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:
                    fprintf(stderr, "SHIFTING dmi. tdi: %ld\n", tdi);

                    // ignore first shift (dtmcontrol_scan_via_bscan() Note the starting offset is bit 1, not bit 0.  In BSCAN tunnel, there is a one-bit TCK skew between output and input)
                    //if (first_shift >= 2) {

                        // preload bit to send
                        tdo = dmi_shift_register & 0x01;

                        // shift data from the tdi register in
                        dmi_shift_register = dmi_shift_register >> 1;

                        // shift in tdi from the left
                        dmi_shift_register |= (tdi << (ABITS_LENGTH + (34 - 1))); // size of dmi container register is variable bits
                    //}
                    //first_shift = 0;

                    if (first_shift == 41) {
                        fprintf(stderr, "SHIFTING dmi. tdi: %ld\n", tdi);
                    }


                    first_shift++;
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
        fprintf(stderr, "*BLINK ON*\n");
        break;

    // b - Blink off
    case 'b': /* fprintf(stderr, "_______\n"); */
        fprintf(stderr, "*BLINK off*\n");
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
            // // 48d == 0x30 == '0', 49 == 0x31 == '1'
            // //fprintf(stderr, "Sending %d\n", tosend);
            // if (tosend == 0x30) {
            //     fprintf(stderr, ">> 0\n");
            // } else {
            //     fprintf(stderr, ">> 1\n");
            // }

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
            fprintf(stderr, "CAPTURE_DR - capturing IDCODE into id_code_shift_register\n");
            id_code_shift_register = id_code_container_register;
            break;

            // TODO case for bypass

        case RiscV_DTM_Registers::DTM_CONTROL_AND_STATUS:
            fprintf(stderr, "CAPTURE_DR - RISCV_DTM_REGISTERS::DTM_CONTROL_AND_STATUS - capturing dtmcs_container_register into dtmcs_shift_register - ");
            print_dtmcs(dtmcs_container_register);
            
            dtmcs_shift_register = dtmcs_container_register;

            first_shift = 1;
            break;

        case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:
            fprintf(stderr, "CAPTURE_DR - RISCV_DTM_REGISTERS::DEBUG_MODULE_INTERFACE_ACCESS - capturing dmi_container_register into dmi_shift_register - ");
            print_dmi(dmi_container_register);
            
            dmi_shift_register = dmi_container_register;

            first_shift = 0;
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

        case RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS:
            fprintf(stderr, "UPDATE_DR RiscV_DTM_Registers::DEBUG_MODULE_INTERFACE_ACCESS\n");

            // print before the change
            print_dmi(dmi_container_register);

            // DEBUG just activate this line again!
            dmi_container_register = dmi_shift_register;

            // print after the change
            print_dmi(dmi_container_register);

            dmi_address = get_dmi_address(dmi_container_register);
            dmi_data = get_dmi_data(dmi_container_register);
            dmi_op = get_dmi_op(dmi_container_register);

            fprintf(stderr, "dmi_address: %ld, dmi_data: %ld, dmi_op: %ld\n", dmi_address, dmi_data, dmi_op);

            // dmi_container_register = 0xFFFFFFFF;
            // dmi_shift_register = 0xFFFFFFFF;

            //dmi_container_register = 0x01;
            //dmi_shift_register = 0x01;

            //dmi_container_register = (0x01 << 2);
            //dmi_shift_register = (0x01 << 2);

            //dmi_container_register = 0x07ffffc1;
            //dmi_shift_register = 0x07ffffc1;
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
    uint64_t address = 0x00;

    // The data to send to the DM over the DMI during UpdateDR, and the data 
    // returned from the DM as a result of the previous operation.
    uint64_t data = 0x00;

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

