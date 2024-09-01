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
        //tdo = jtag_tdo;
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

void remote_bitbang_t::set_pins(char _tck, char _tms, char _tdi)
{
    tck = _tck;
    tms = _tms;
    tdi = _tdi;

    // on the rising edge, the TAP state machine transitions
    if (_tck != 0) {
        tsm_state_machine.transition(_tms);
    }
}

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
        fprintf(stderr, "remote_bitbang got unsupported command '%c'\n",
                command);
    }

    if (dosend)
    {
        while (1)
        {
            // 48d == 0x30 == '0', 49 == 0x31 == '1'
            fprintf(stderr, "Sending %d\n", tosend);

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

    if (quit)
    {
        // The remote disconnected.
        fprintf(stderr, "Remote end disconnected\n");
        close(client_fd);
        client_fd = 0;
    }
}

void remote_bitbang_t::state_entered(tsm_state new_state)
{
    //fprintf(stderr, "state_entered\n");

    switch (new_state) {

        // In this state all test-modes (for example extest-mode) are reset, which will disable their operation, allowing the chip to follow its normal operation.
        case TEST_LOGIC_RESET:
            fprintf(stderr, "TEST_LOGIC_RESET entered\n");

            // TODO: write IDCODE value into DR!
            // see: https://openocd.org/doc/pdf/openocd.pdf#page=69&zoom=100,120,96
            instruction_register = Instruction::IDCODE;

            break;

        // This is the resting state during normal operation.
        case RUN_TEST_IDLE:
            fprintf(stderr, "RUN_TEST_IDLE entered\n");
            break;

        // These are the starting states respectively for accessing one of the data registers 
        // (the boundary-scan or bypass register in the minimal configuration) or the instruction register.
        case SELECT_DR_SCAN:
            fprintf(stderr, "SELECT_DR_SCAN entered\n");

            // TODO: select the data register which is indexed by the current value of IR
            // Remember: during TAP reset, IR is preloaded with the index of the IDCODE register!
            // (Or also possible the bypass register)
            // 
            // Lets assume this simulated TAP enters IDCODE into IR on reset. This means during 
            // SELECT_DR_SCAN, the IDCODE register is selected.

            

            break;
        case SELECT_IR_SCAN:
            fprintf(stderr, "SELECT_IR_SCAN entered\n");
            break;

        // These capture the current value of one of the data registers or the instruction register respectively 
        // into the scan cells. This is a slight misnomer for the instruction register, since it is usual 
        // to capture status information, rather than the actual instruction with Capture-IR.
        case CAPTURE_DR:
            fprintf(stderr, "CAPTURE_DR entered\n");

            // TODO: copy the value in the selected data register into the data shift register

            switch (instruction_register) {

                case Instruction::IDCODE:
                    id_code_shift_register = id_code_register;
                    break;

                default:
                    fprintf(stderr, "[Error] Unknown instruction register!!!\n");
                    break;
                
            }

            break;
        case CAPTURE_IR:
            fprintf(stderr, "CAPTURE_IR entered\n");
            break;

        // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO (on the falling edge of TCK) from the currently selected data or instruction register respectively.
        case SHIFT_DR:
            //fprintf(stderr, "SHIFT_DR entered\n");
            fprintf(stderr, "SHIFT_DR entered ");

            // shift the data shift register which places the rightmost bit into tdo for subsequent reads to pick up.
            switch (instruction_register) {

                case Instruction::IDCODE:
                    tdo = id_code_shift_register & 0x01;
                    id_code_shift_register = id_code_shift_register >> 1;
                    break;

                default:
                    fprintf(stderr, "[Error] Unknown instruction register!!!\n");
                    break;
                
            }

            break;
        case SHIFT_IR:
            fprintf(stderr, "SHIFT_IR entered\n");
            break;

        // These are the exit states for the corresponding shift state. From here the state machine can either enter a pause state or enter the update state.
        case EXIT1_DR:
            fprintf(stderr, "EXIT1_DR entered\n");
            break;
        case EXIT1_IR:
            fprintf(stderr, "EXIT1_IR entered\n");
            break;

        // Pause in shifting data into the data or instruction register. This allows for example test equipment supplying TDO to reload buffers etc.
        case PAUSE_DR:
            fprintf(stderr, "PAUSE_DR entered\n");
            break;
        case PAUSE_IR:
            fprintf(stderr, "PAUSE_IR entered\n");
            break;

        // These are the exit states for the corresponding pause state. From here the state machine can either resume shifting or enter the update state.
        case EXIT2_DR:
            fprintf(stderr, "EXIT2_DR entered\n");
            break;
        case EXIT2_IR:
            fprintf(stderr, "EXIT2_IR entered\n");
            break;

        // The value shifted into the scan cells during the previous states is driven into the chip (from inputs) or onto the interconnect (for outputs).
        case UPDATE_DR:
            fprintf(stderr, "UPDATE_DR entered\n");
            break;
        case UPDATE_IR:
            fprintf(stderr, "UPDATE_IR entered\n");
            break;

        default:
            fprintf(stderr, "[Error] Unknown state!!!\n");
            return;
    }
}