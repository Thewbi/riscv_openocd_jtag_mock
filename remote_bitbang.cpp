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

/////////// remote_bitbang_t
remote_bitbang_t::remote_bitbang_t(uint16_t port) : socket_fd(0),
                                                    client_fd(0),
                                                    recv_start(0),
                                                    recv_end(0),
                                                    err(0),
                                                    tsm_state_machine(this)
{
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
    // fprintf(stderr, "tick()\n");

    if (client_fd > 0)
    {
        // fprintf(stderr, "tick() execute_command()\n");

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
    fprintf(stderr, "set_pins() retrieved input: tck:%d, tms:%d, tdi:%d\n", _tck, _tms, _tdi);

    tck = _tck;
    tms = _tms;
    tdi = _tdi;

    // on the rising edge, the TAP state machine transitions
    if (_tck != 0) {

        tsm_state_machine.transition(_tms, static_cast<uint8_t>(_tck));

    } else {

        switch (tsm_state_machine.tsm_current_state)
        {

            // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO
            // (on the falling edge of TCK) from the currently selected data or instruction register respectively.
            case SHIFT_DR:
                //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. ", rising_edge_clk);
                //fprintf(stderr, "SHIFT_DR entered.\n");

                // shift the data shift register which places the rightmost bit into tdo for subsequent reads to pick up.
                switch (static_cast<Instruction>(instruction_container_register))
                {

                case Instruction::IDCODE:

                    //if (rising_edge_clk)
                    //{
                        //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. SHIFT ", rising_edge_clk);

                        // prepare send (which happens on the falling edge of the clock)
                        // preload the pin with the value that is shifted out from the shift register
                        tdo = id_code_shift_register & 0x01;

                        // shift data from the tdi register in
                        id_code_shift_register = id_code_shift_register >> 1;
                        id_code_shift_register |= (tdi << (32 - 1)); // size of idcode container register is 32 bits
                    //}

                    break;

                // case Instruction::BYPASS:
                //     fprintf(stderr, "[Error] Unknown instruction register!!! BYPASS\n");
                //     break;

                default:
                    fprintf(stderr, "[Error] Unknown instruction register!!! %d \n", instruction_container_register);
                    break;
                }

                break;
                
            // Shift in a bit from tdi into IR (on the rising edge) and also out from the IR to tdi (on the falling edge)
            case SHIFT_IR:
                //fprintf(stderr, "SHIFT_IR entered. rising_edge_clk: %d. \n", rising_edge_clk);
                //fprintf(stderr, "SHIFT_IR entered\n");

                //if (rising_edge_clk)
                //{
                    // prepare send (which happens on the falling edge of the clock)
                    // preload the pin with the value that is shifted out from the shift register
                    tdo = instruction_shift_register & 0x01;

                    fprintf(stderr, "Before: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

                    // shift data from the tdi register in
                    instruction_shift_register = instruction_shift_register >> 1;
                    instruction_shift_register |= (tdi << (5 - 1)); // size of ircode container register is 5 bits

                    fprintf(stderr, "After: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

                //}

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
        }
        else
        {
            again = 0;
        }
    }

    // fprintf(stderr, "Received a command %c\n", command);
    fprintf(stderr, "%c ", command);

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
            // 48d == 0x30 == '0', 49 == 0x31 == '1'
            // fprintf(stderr, "Sending %d\n", tosend);

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

    switch (new_state)
    {

    // In this state all test-modes (for example extest-mode) are reset, which will disable their operation, allowing the chip to follow its normal operation.
    case TEST_LOGIC_RESET:
        fprintf(stderr, "TEST_LOGIC_RESET entered\n");

        // TODO: write IDCODE value into DR!
        // see: https://openocd.org/doc/pdf/openocd.pdf#page=69&zoom=100,120,96
        instruction_container_register = static_cast<uint8_t>(Instruction::IDCODE);

        break;

    // This is the resting state during normal operation.
    case RUN_TEST_IDLE:
        fprintf(stderr, "RUN_TEST_IDLE entered\n");
        break;

    // These are the starting states respectively for accessing one of the data registers
    // (the boundary-scan or bypass register in the minimal configuration) or the instruction register.
    case SELECT_DR_SCAN:
        fprintf(stderr, "SELECT_DR_SCAN entered\n");

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
        fprintf(stderr, "SELECT_IR_SCAN entered\n");
        break;

    // These capture the current value of one of the data registers or the instruction register respectively
    // into the scan cells. This is a slight misnomer for the instruction register, since it is usual
    // to capture status information, rather than the actual instruction with Capture-IR.
    case CAPTURE_DR:
        fprintf(stderr, "CAPTURE_DR entered\n");

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

        switch (static_cast<Instruction>(instruction_container_register))
        {

        case Instruction::IDCODE:
            fprintf(stderr, "CAPTURE_DR - capturing IDCODE into id_code_shift_register\n");
            id_code_shift_register = id_code_container_register;
            break;

            // TODO case for bypass

        default:
            fprintf(stderr, "[Error] Unknown instruction register!!!\n");
            break;
        }

        break;
    case CAPTURE_IR:
        fprintf(stderr, "CAPTURE_IR entered\n");

        // copy data from the IR container register into the IR shift register.
        // the length of the IR register can be specified via the openocd.cfg file.
        // It is set to 8 in this example.
        instruction_shift_register = static_cast<uint8_t>(instruction_container_register);
        break;

    // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO
    // (on the falling edge of TCK) from the currently selected data or instruction register respectively.
    case SHIFT_DR:
        //fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. ", rising_edge_clk);
        fprintf(stderr, "SHIFT_DR entered.\n");
/*
        // shift the data shift register which places the rightmost bit into tdo for subsequent reads to pick up.
        switch (static_cast<Instruction>(instruction_container_register))
        {

        case Instruction::IDCODE:

            if (rising_edge_clk)
            {
                fprintf(stderr, "SHIFT_DR entered. rising_edge_clk: %d. SHIFT ", rising_edge_clk);

                // prepare send (which happens on the falling edge of the clock)
                // preload the pin with the value that is shifted out from the shift register
                tdo = id_code_shift_register & 0x01;

                // shift data from the tdi register in
                id_code_shift_register = id_code_shift_register >> 1;
                id_code_shift_register |= (tdi << (32 - 1)); // size of idcode container register is 32 bits
            }

            break;

        // case Instruction::BYPASS:
        //     fprintf(stderr, "[Error] Unknown instruction register!!! BYPASS\n");
        //     break;

        default:
            fprintf(stderr, "[Error] Unknown instruction register!!! %d \n", instruction_container_register);
            break;
        }
*/
        break;

    // Shift in a bit from tdi into IR (on the rising edge) and also out from the IR to tdi (on the falling edge)
    case SHIFT_IR:
        fprintf(stderr, "SHIFT_IR entered. rising_edge_clk: %d. \n", rising_edge_clk);
        //fprintf(stderr, "SHIFT_IR entered\n");

/*
        if (rising_edge_clk)
        {
            // prepare send (which happens on the falling edge of the clock)
            // preload the pin with the value that is shifted out from the shift register
            tdo = instruction_shift_register & 0x01;

            fprintf(stderr, "Before: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

            // shift data from the tdi register in
            instruction_shift_register = instruction_shift_register >> 1;
            instruction_shift_register |= (tdi << (5 - 1)); // size of ircode container register is 5 bits

            fprintf(stderr, "After: tdi: %d instruction_shift_register %d\n", tdi, instruction_shift_register);

        }
*/
        break;

    // These are the exit states for the corresponding shift state.
    // From here the state machine can either enter a pause state or enter the update state.
    case EXIT1_DR:
        fprintf(stderr, "EXIT1_DR entered\n");
        break;
    case EXIT1_IR:
        fprintf(stderr, "EXIT1_IR entered\n");
        break;

    // Pause in shifting data into the data or instruction register.
    // This allows for example test equipment supplying TDO to reload buffers etc.
    case PAUSE_DR:
        fprintf(stderr, "PAUSE_DR entered\n");
        break;
    case PAUSE_IR:
        fprintf(stderr, "PAUSE_IR entered\n");
        break;

    // These are the exit states for the corresponding pause state.
    // From here the state machine can either resume shifting or enter the update state.
    case EXIT2_DR:
        fprintf(stderr, "EXIT2_DR entered\n");
        break;
    case EXIT2_IR:
        fprintf(stderr, "EXIT2_IR entered\n");
        break;

    // The value shifted into the scan cells during the previous states is driven into the chip (from inputs) or onto the interconnect (for outputs).
    case UPDATE_DR:
        fprintf(stderr, "UPDATE_DR entered\n");
        switch (static_cast<Instruction>(instruction_container_register))
        {

        case Instruction::IDCODE:
            id_code_container_register = id_code_shift_register;
            break;

        default:
            fprintf(stderr, "[Error] Unknown instruction register!!!\n");
            break;
        }
        break;
    case UPDATE_IR:
        fprintf(stderr, "UPDATE_IR entered\n");
        instruction_container_register = instruction_shift_register;
        break;

    default:
        fprintf(stderr, "[Error] Unknown state!!!\n");
        return;
    }
}