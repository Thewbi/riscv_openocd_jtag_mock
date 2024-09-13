# riscv_openocd_jtag_mock
Linux Server for localhost that openocd with RISC-V support can connect to in bitbanged JTAG

# Compile openocd with RISCV support on Linux

See also here: https://www.francisz.cn/2020/03/07/riscv-openocd/

First clone the openocd source code including the RISCV features.

```
cd ~/dev/openocd
git clone https://github.com/riscv-collab/riscv-openocd
```

In the latest version of the riscv-openocd repository, jimtcl is included
as a subproject and the ./bootstrap command will automatically clone jimtcl.
The next steps are optional with the latest version of riscv-openocd.
In the latest version, you should not copy jimtcl manually! This will make
the installation fail!

%%% OPTIONAL START

Now clone the tcl implementation and copy it into the openocd folder

```
cd ~/dev/openocd
git clone https://github.com/msteveb/jimtcl.git
cp -r jimtcl riscv-openocd/jimtcl
```

%%% OPTIONAL END

Start the compilation

```
cd ~/dev/openocd
cd riscv-openocd
./bootstrap
```

Inside the next statement, replace <USERNAME> by your linux user before
executing.

```
./configure --prefix=/home/<USERNAME>/openocd/ --enable-remote-bitbang
```

Compile

```
make clean
make
make install
```

Inside the next statement, replace <USERNAME> by your linux user before
executing.

```
/home/<USERNAME>/openocd/bin/openocd -v
```


# Create a config file for openocd

See https://openocd.org/doc/html/Debug-Adapter-Configuration.html

In order to connect openocd not to a USB JTAG adapter but to a port on
localhost where openocd will talk the bitbanged variant of JTAG to,
create a config file that applies the remote_bitbank driver.

```
adapter driver remote_bitbang
remote_bitbang port 3335
remote_bitbang host localhost

transport select jtag
```

This configuration file is pretty straightforward.
As a driver, remote_bitbang is selected. The remote address is localhost:3335.
The transport protocol to speak is set to jtag (swd is another possible option)

This configuration file is more complex:

```
adapter driver remote_bitbang
remote_bitbang port 3335
remote_bitbang host localhost

transport select jtag

# define chipname variable (why the leading underscore?)
set _CHIPNAME riscv
jtag newtap $_CHIPNAME cpu -irlen 8

set _TARGETNAME $_CHIPNAME.cpu
target create $_TARGETNAME riscv -chain-position $_TARGETNAME

gdb_report_data_abort enable

init
halt
```
# Start openocd

Make sure that the mock server is started so that openocd can connect to it.

Now start openocd using the configuration file.

A word about logging levels.
-d will enable logging level 3.
-d<level> will allow you to set the logging level.

Level 0 is error messages only; 
Level 1 adds warnings; 
Level 2 adds informational messages; 
Level 3 adds debugging messages; and 
Level 4 adds verbose low-level debug messages. 

```
cd /home/<USERNAME>/dev/openocd/riscv_openocd_jtag_mock
set JTAG_VPI_PORT=36054;
set JTAG_DTM_ENABLE_SBA=on; 
/home/<USERNAME>/openocd/bin/openocd -f remote_bitbang.cfg -d4 -l log
```

```
cd /home/wbi/dev/openocd/riscv_openocd_jtag_mock
set JTAG_VPI_PORT=36054;
set JTAG_DTM_ENABLE_SBA=on;
/home/wbi/openocd/bin/openocd -f remote_bitbang.cfg -d4 -l log
```

openocd will not print to the console since the -l parameter has been passed.
Instead openocd will write into the a file called log as specified on the 
command line.

When openocd is run like this right now, it will fail to connect to the
server on localhost:3335 since this server does not exist yet!

# Create the server (= mocked target)

```
g++ -g remote_bitbang_main.cpp remote_bitbang.cpp tap_state_machine.cpp tap_state_machine_callback.cpp 

clear & ./a.out

/home/wbi/openocd/bin/openocd -d -f remote_bitbang.cfg -d4 -l log
```

# Interpreting the commands that openocd sends

When openocd connects to the mock server it will send a bunch of commands.
Let's see what openocd does.

The individual commands are defined in this document:
https://github.com/openocd-org/openocd/blob/master/doc/manual/jtag/drivers/remote_bitbang.txt

blink on
	Blink a light somewhere. The argument on is either 1 or 0.

read
	Sample the value of tdo.

write tck tms tdi
	Set the value of tck, tms, and tdi.

reset trst srst
	Set the value of trst, srst.

swdio_drive
	Set the output enable of the bidirectional swdio (tms) pin

swdio_read
	Sample the value of swdio (tms).

swd_write
	Set the value of swclk (tck) and swdio (tms).

(optional) sleep
	Instructs the remote host to sleep/idle for some period of time before
		executing the next request

The commands above are encoded in a more concise format which leads to the single character commands
that openocd actually sends to the mock server.

```
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
```

```
B b r B b B 2 6 2 6 2 6 2 6 2 6 2 6 2 6 2 b B 2 6 2 6 2 6 0 4 2 6 0 4 0 4 0 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 1 R 5 3 R 7 0 4 0 2 6 2 6 2 6 2 6 2 6 2 6 2 6 2 b B 2 6 2 6 0 4 2 6 2 6 0 4 0 4 0 1 R 5 3 R 7 2 6 0 4 0 b B 2 6 2 6 2 6 2 6 2 6 2 6 2 6 2 b Q Remote end disconnected
```

# Reseting the system

```
B b r B b B
```

Turns an LED on (B = blink = LED on, b = LED off) and performs a r operation which is a reset (Reset 0 0)
Reset 0 0 means that the command reset trst srst is performed. trst and srst are set to 0. 

trst stands for TAP reset and srst for system reset. Both signals are active low. A zero therefore triggers
the resets.

trst moves the state machine directly into the state Test_Logic_Reset (see https://en.wikipedia.org/wiki/JTAG)

# Resetting the TAP's statemachine to the Test-Logic-Reset state

TAP is the Test Access Port which is the module that the JTAG client (openocd) will talk to. The TAP is the
device that terminates the JTAG connection coming in from the outside. It will interpret the commands 
and translate them to the necessary interal implementation to cause the system to perform actions.

The TAP has a statemachine which consists of 16 states. The initial state it is in after power on reset is
unknown. Therefore openocd sends commands to make the state machine transition back to the Test-Logic-Reset
state which is the start state of the state machine.

The state machine transitions when it sees a 1 or a 0 (there are no other signals!). To send a signal to 
the state machine, the signal is placed onto the JTAG tms pin and the clock is toggled.

All tmi pins are connected in parallel to all elements in the daisy chain. This means that each element
sees the same tmi values. This in turn means that each state machine in each element performs the
exact same state transitions and each state machine is in the same state at each point in time! It is
as if the JTAG client controls all TAPs at the same time!

In order to get from an arbitrary state back to the Test-Logic-Reset state, it is enough to send five 
1 signals to the state machine and toggle the clock each time.

openocd therefore has to set tms to 1 and toggle the clock five times. This translates to the command
sequence

```
write tck=0 tms=1 tdi=0
write tck=1 tms=1 tdi=0

write tck=0 tms=1 tdi=0
write tck=1 tms=1 tdi=0

write tck=0 tms=1 tdi=0
write tck=1 tms=1 tdi=0

write tck=0 tms=1 tdi=0
write tck=1 tms=1 tdi=0

write tck=0 tms=1 tdi=0
write tck=1 tms=1 tdi=0
```

In concise form, this yields the command sequence

```
2 6 
2 6 
2 6 
2 6 
2 6
```

0 - Write tck:0 tms:0 tdi:0     ----- WRITE/CLOCK TMS 0 and TDI 0
1 - Write tck:0 tms:0 tdi:1 ----- WRITE/CLOCK TDI 1
2 - Write tck:0 tms:1 tdi:0 ----- WRITE/CLOCK TMS 1
3 - Write tck:0 tms:1 tdi:1
4 - Write tck:1 tms:0 tdi:0     ----- WRITE/CLOCK TMS 0 and TDI 0
5 - Write tck:1 tms:0 tdi:1 ----- WRITE/CLOCK TDI 1
6 - Write tck:1 tms:1 tdi:0 ----- WRITE/CLOCK TMS 1
7 - Write tck:1 tms:1 tdi:1    

Looking at the commands which openocd actually sends, there are more than 5 toggles but this
really does not matter. The TAP state is Test-Logic-Reset in any case.

Assumption: r has placed the state machine into Test-Logic-Reset immediately.
Also: According to: https://www.fpga4fun.com/JTAG3.html
> "Unlike for the BYPASS instruction, the IR value for the IDCODE is not standard. Fortunately, each time the TAP controller goes into Test-Logic-Reset, it goes into IDCODE mode (and loads the IDCODE into DR)."

This means that the openocd client does not have to load perform any actions, it will immediately
find IDCODE inside the DR and can shift it out.

According to this document: https://openocd.org/doc/pdf/openocd.pdf#page=69&zoom=100,120,96
The DR may contain either IDCODE or BYPASS! It is unknown which is really contained!

State changes on the rising edge, this means when the CLK goes from 0 to 1.

```
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 - clock is set to 0, STATE: Test-Logic-Reset
b B - LED off, LED on
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
2 6 INPUT: TMS=1, STATE: Test-Logic-Reset
0 4 INPUT: TMS=0, STATE: Run-Test / Idle
2 6 INPUT: TMS=1, STATE: SELECT DR-Scan
0 4 INPUT: TMS=0, STATE: CAPTURE-DR   ------- CAPUTE-DR will load the value of the register (now ICODE) into the shift register!
0 4 INPUT: TMS=0, STATE: SHIFT-DR
0 1 INPUT: TMS=0, STATE: SHIFT-DR Data:1 -- shift a one into the shift register and shift out the data to the tdo pin on the other side

R 5 1 TMS=0, STATE: SHIFT-DR
R 5 1 TMS=0, STATE: SHIFT-DR
R 5 1 TMS=0, STATE: SHIFT-DR
...
```

openocd the decides to blink the LED once more and it keeps resetting the state machine.

```
b B 2 6 2 6 2 6 
```

After that it transitions into the SHIFT-DR state. On the way to the SHIFT-DR state
it passes by the CAPTURE-DR state. Here the value of the register indexed by the IR register which
is IDCODE after reset, is loaded into the DR register.

```
0 4 2 6 0 4 0 4 
```

It then remains in the "SHIFT-DR state" state and then shifts in a one into the DR register on the left
and shifts out the bits to tdo on the right.

```
0 1
```
After that we get 670 (R 5 1) instructions.

R is the instruction "read request" which writes a single 1 or 0 onto tdo.
5 and 1 both write the value 1 to the tdi pin and toggle the clock while containing
a value of 0 for the TMS mode select which makes the state machine stay in the current
SHIFT-DR state.

5 - Write tck:1 tms:0 tdi:1
1 - Write tck:0 tms:0 tdi:1

I think this will shift our the entire description stored in the IDCODE register to the tdo pin.

This might be the attempt to write the BYPASS instruction / register address into the
IR register. But the state machine is not in Shift-IR state! I really do not understand
what openocd is doing.

```
Debug: 66 555 command.c:153 script_debug(): command - target names
Debug: 67 555 command.c:153 script_debug(): command - target names

R 7 0 4 0 2 6 2 6 2 6 2 6 2 6 2 6 2 6 2
```



# Understanding Register Select

JTAG devices have a single IR register and may have several DR registers.

IR stands for instruction register.
DR stands for data register.

To load values into the IR register, the TAP state machine has to be in Shift-IR state.

To load values into the DR register:
1. go into Shift-IR state
2. shift the index of the data register into the IR register
3. go into Shift-DR state
4. shift the data into the DR register (that is currently indexed by the IR-register)

Therefore the name IR (Instruction register) is a rather bad name for the instruction register
since it will not only contain instructions but also the indexes of the data registers to shift
into while in Shift-DR state!

The sizes (widths) of the IR register is unknown to the JTAG client! The JTAG specification says
that the IR register has to be at least 5 bit wide.




# Determine, how many elements are in the JTAG chain

To understand what a JTAG chain is, look at the page https://www.fpga4fun.com/JTAG2.html
If there are several chips to be debugged via JTAG, they are daisy chained together.

openocd first wants to know how many devices it can actually talk to, i.e. how long
the daisy / JTAG chain really is.

To determine how many devices are in the daisy chain, delay counting is used. Every
JTAG devices in the daisy chain has to implement an instruction called BYPASS.
To execute the BYPASS instruction fill the entire IR register with ones.
Since the JTAG client does not know how wide the IR register is, it will shift 100
1s into the IR register to make sure it triggers the BYPASS instruction.

Once BYPASS is selected, the JTAG client sends a TMS bit and toggles to clock to 
transition the state machine into to exit the Shift-IR state and enter the Exit1-IR
state.

Then the JTAG client transitions the state machine into the Shift-DR state.

When the BYPASS instruction is inside the IR-Register and the state machine is
in Shift-DR state and 1s are send to the tdi (JTAG test data in) pin, then these 1s 
are shifted into the BYPASS register. The BYPASS register outputs it's value with
a delay of 1.

Since the data output is daisy chained to the data input of the next element, 
the delay will propagate through the chain and increase by 1 per element in the chain.

The TJAG client can start to send in a one and count the delays to determine how many
elements are in the daisy chain.

For example if it sends in a 1 and reads a 1, then there is no delay and there are
no elements in the chain. This is a dumb example since there must be at least one
element in the chain.

If the client sends in a 1 and reads a 0 and then reads a 1, there is a delay of 1 
and hence there is a single element in the chain.

If the client sends in a 1 and reads four 0 before reading a 1, then there are 4
elements in the chain.

# Move the TAP's state machine to shift-IR state

Therefore it sends 





# Reading dtmcontrol

```
static int riscv_examine(struct target *target)
{
	LOG_TARGET_DEBUG(target, "Starting examination");
	if (target_was_examined(target)) {
		LOG_TARGET_DEBUG(target, "Target was already examined.");
		return ERROR_OK;
	}

	/* Don't need to select dbus, since the first thing we do is read dtmcontrol. */

	RISCV_INFO(info);
	uint32_t dtmcontrol;
	if (dtmcontrol_scan(target, 0, &dtmcontrol) != ERROR_OK || dtmcontrol == 0) {
		LOG_TARGET_ERROR(target, "Could not read dtmcontrol. Check JTAG connectivity/board power.");
		return ERROR_FAIL;
	}

    ...
```


# VSCode openocd launch configuration for debugging

```
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "C++ Launch",
            "type": "cppdbg",
            "request": "launch",
            "program": "/home/wbi/openocd/bin/openocd",
            "cwd": "/home/wbi/dev/openocd/riscv_openocd_jtag_mock",
            "args": ["-f", "remote_bitbang.cfg", "-d4", "-l", "log"],
            "stopAtEntry": true,
            "launchCompleteCommand": "exec-run",
            "linux": {
              "MIMode": "gdb",
              "miDebuggerPath": "/usr/bin/gdb"
            },
            "osx": {
              "MIMode": "lldb"
            },
            "windows": {
              "MIMode": "gdb",
              "miDebuggerPath": "C:\\MinGw\\bin\\gdb.exe"
            }
          }
    ]
}
```

## VSCode mock launch configuration for debugging

```
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "C++ Launch",
            "type": "cppdbg",
            "request": "launch",
            "program": "/home/wbi/dev/openocd/riscv_openocd_jtag_mock/a.out",
            "cwd": "/home/wbi/dev/openocd/riscv_openocd_jtag_mock",
            //"args": ["-f", "remote_bitbang.cfg", "-d4", "-l", "log"],
            "stopAtEntry": true,
            "launchCompleteCommand": "exec-run",
            "linux": {
              "MIMode": "gdb",
              "miDebuggerPath": "/usr/bin/gdb"
            },
            "osx": {
              "MIMode": "lldb"
            },
            "windows": {
              "MIMode": "gdb",
              "miDebuggerPath": "C:\\MinGw\\bin\\gdb.exe"
            }
          }
    ]
}
```




## How openocd determines xlen

Source: https://riscv.org/wp-content/uploads/2017/05/riscv-privileged-v1.10.pdf
Source: https://forums.sifive.com/t/what-is-the-right-way-to-read-misa-register/3939/5

I think that the standard approach to determining a RISC-V CPU's ISA (???)
is to read the misa register (0x301). It will contain XLEN (= the CPU's bit width, 32, 64, 128)
and also which extensions the CPU supports.

Remember: When an external debugger reads the misa register, it does not know, what XLEN is yet.
This means it does not know how many bits the misa register really has! The information is therefore
stored at the leftmost border and at the rightmost border!

The first two (most significant) bits (MXL) contain the XLEN which is decoded using the table below:

MXL XLEN
1   32
2   64
3   128

The ISA is encoded using the 26 letters of the alphabet which are encoded as a bitfield:
This bit field is stored using the least significant bits, that means at the right border of the misa register.

Bit   Character Description
0     A Atomic extension
1     B Tentatively reserved for Bit operations extension
2     C Compressed extension
3     D Double-precision floating-point extension
4     E RV32E base ISA
5     F Single-precision floating-point extension
6     G Additional standard extensions present (IMAFD)
7     H Reserved (H - Hypervisor, also Virtualisierung)
8     I RV32I/64I/128I base ISA
9     J Tentatively reserved for Dynamically Translated Languages extension
10    K Reserved
11    L Tentatively reserved for Decimal Floating-Point extension
12    M Integer Multiply/Divide extension
13    N User-level interrupts supported
14    O Reserved
15    P Tentatively reserved for Packed-SIMD extension
16    Q Quad-precision floating-point extension
17    R Reserved
18    S Supervisor mode implemented
19    T Tentatively reserved for Transactional Memory extension
20    U User mode implemented
21    V Tentatively reserved for Vector extension
22    W Reserved
23    X Non-standard extensions present
24    Y Reserved
25    Z Reserved

https://wiki.riscv.org/display/HOME/Ratified+Extensions
https://riscv.org/wp-content/uploads/2019/12/riscv-spec-20191213.pdf
https://five-embeddev.com/riscv-priv-isa-manual/Priv-v1.12/machine.html

Zicsr - Control & Status Register Zugriff (Chapter 9 “Zicsr”, Control and Status Register (CSR) Instructions, Version 2.0. Page 55)
Zifencei - Synchronisation von Lese/Schreibzugriffen auf Befehlsspeicher
Zbb, Zbc - Bitmanipulation

If the misa register is not implemented (CPU returns 0x00), then the external debugger
has to use a "separate non-standard mechanism". (see https://riscv.org/wp-content/uploads/2017/05/riscv-privileged-v1.10.pdf)

OpenOCD tries to request the value in register s0 using a width of 64 bit.
When a 32 RISCV cpu receives this request, then it has to answer with an error.
OpenOCD then falls back to 32bit.
OpenOCD does not even consider RISC 128bit as an option!

```
static int examine_xlen(struct target *target)
{
	RISCV_INFO(r);
	unsigned int cmderr;

	const uint32_t command = riscv013_access_register_command(target,
			GDB_REGNO_S0, /* size */ 64, AC_ACCESS_REGISTER_TRANSFER);
	int res = riscv013_execute_abstract_command(target, command, &cmderr);
	if (res == ERROR_OK) {
		r->xlen = 64;
		return ERROR_OK;
	}
	if (res == ERROR_TIMEOUT_REACHED)
		return ERROR_FAIL;
	r->xlen = 32;

	return ERROR_OK;
}
```





## Resetting the DM

Through the DMI register, the external debugger (ED) can talk to the RISC-V DM.


```
Debug: 274 676 riscv-013.c:537 check_dbgbase_exists(): [riscv.cpu0] Searching for DM with DMI base address (dbgbase) = 0x0
``` 

```
Debug: 338 995 riscv-013.c:1817 reset_dm(): [riscv.cpu0] Initiating DM reset.
```




```
Debug: 9366 14598 riscv.c:3684 riscv_openocd_poll(): [riscv.cpu0] Polling all harts.
```

```
Debug: 2959 22657 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register misa
Debug: 3377 31740 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register vlenb
Debug: 3382 36152 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register mstatus
Debug: 4891 45706 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register mtopi
Debug: 5308 53308 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register mtopei
Debug: 5727 62459 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register dcsr
Debug: 2959 22657 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register misa

Debug: 436 9660 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register zero
Debug: 454 10252 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register ra
Debug: 471 10856 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register sp
Debug: 488 11460 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register gp
Debug: 505 12064 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register tp
Debug: 522 12668 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t0
Debug: 539 13272 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t1
Debug: 556 13876 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t2
Debug: 573 14480 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register fp
Debug: 590 15084 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s1

Debug: 607 15688 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a0
Debug: 624 16292 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a1
Debug: 641 16896 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a2
Debug: 658 17500 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a3
Debug: 675 18104 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a4
Debug: 692 18708 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a5
Debug: 709 19312 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a6
Debug: 726 19916 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register a7

Debug: 743 20520 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s2
Debug: 760 21124 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s3
Debug: 777 21728 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s4
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s5
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s6
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s7
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s8
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s9
Debug: 794 22350 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s10
Debug: 454 10252 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register s11

Debug: 913 26560 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t3
Debug: 913 26560 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t4
Debug: 913 26560 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t5
Debug: 454 10252 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register t6

Debug: 981 28972 riscv-013.c:4916 riscv013_get_register(): [riscv.cpu0] reading register dpc
```



load_image filename [address ['bin'|'ihex'|'elf'|'s19' [min_address [max_length]]]]

```
load_image tutorials/de0_nano/hello.elf
load_image test.hex 0x00000000 ihex
load_image hello_world_mt.hex 0x00000000 ihex
``` 





## Telnet into openocd

```
telnet 127.0.0.1 4444
telnet localhost 4444
help
```

```
load_image create_binary_example/example.hex 0x00000000 ihex
```

```
sleep msec [busy]
halt [ms]
wait_halt [ms]
resume [address]

step [address] // address is optional, it is possible to just enter step
example:
step 0x00

reset
reset run
reset halt
reset init
soft_reset_halt

# Display contents of address addr, as 64-bit doublewords (mdd), 32-bit words (mdw),
16-bit halfwords (mdh), or 8-bit bytes (mdb).
mdd [phys] addr [count]
mdw [phys] addr [count]
mdh [phys] addr [count]
mdb [phys] addr [count]

# Writes the specified doubleword (64 bits), word (32 bits), halfword (16 bits), or byte
(8-bit) value, at the specified address addr.
mwd [phys] addr doubleword [count]
mww [phys] addr word [count]
mwh [phys] addr halfword [count]
mwb [phys] addr byte [count]

dump_image filename address size
load_image filename address [[bin|ihex|elf|s19] min_addr max_length]
example:
load_image create_binary_example/example.hex 0x00000000 ihe

# With no parameters, lists all active breakpoints. 
# Else sets a breakpoint on code execution starting at address for length bytes. 
bp [address len [hw]]
example:
bp 0x00 4

# Remove the breakpoint at address.
rbp address

# Remove data watchpoint on address
rwp address

# With no parameters, lists all active watchpoints. 
# Else sets a data watchpoint on data from address for length bytes.
wp [address len [(r|w|a) [value [mask]]]]
``` 




Working:

```
jtag names
riscv info

// reading riscv registers (by ABI name)
get_reg zero
get_reg ra
get_reg sp
get_reg gp
// similar with the ABI names: tp, t0-t6, fp, a0-a7, s1-s11, dpc
//
// register index | register abi name
// 0              | zero
// 1              | ra
// 2              | sp
// 3              | gp
// 4              | tp
// 5              | t0
// 6              | t1
// 7              | t2
// 8              | fp
// 9              | s1
// 10             | a0
// 11             | a1
// 12             | a2
// 13             | a3
// 14             | a4
// 15             | a5
// 16             | a6
// 17             | a7
// 18             | s2
// 19             | s3
// 20             | s4
// 21             | s5
// 22             | s6
// 23             | s7
// 24             | s8
// 25             | s9
// 26             | s10
// 27             | s11
// 28             | t3
// 29             | t4
// 30             | t5
// 31             | t6


// reading DebugModuleInterface (DMI) registers
???

// reading/writing DebugModule (DM) registers
riscv dm_read <reg_address>
riscv dm_write <reg_address> value

Example: (Read 0x10 == Debug Module Control) (See page 26 for a list of DM register addresses)
riscv dm_read 0x10
```

Not working:

```
reset halt;
program firmware.elf verify reset;

sleep 0;
exit;

get_reg 0
```

```
adapter
      adapter command group (command valid any time)
  adapter assert |deassert [srst|trst [assert|deassert srst|trst]]
        Controls SRST and TRST lines.
  adapter deassert |assert [srst|trst [deassert|assert srst|trst]]
        Controls SRST and TRST lines.
  adapter driver driver_name
        Select a debug adapter driver (configuration command)
  adapter gpio [
            
            do|tdi|tms|tck|trst|swdio|swdio_dir|swclk|srst|led[gpio_number]
            [-chip chip_number] [-active-high|-active-low]
            [-push-pull|-open-drain|-open-source]
            e|-pull-up|-pull-down][-init-inactive|-init-active|-init-input]
            ]
        gpio adapter command group (configuration command)
  adapter list
        List all built-in debug adapter drivers (command valid any time)
  adapter name
        Returns the name of the currently selected adapter (driver)
        (command valid any time)
  adapter serial serial_string
        Set the serial number of the adapter (configuration command)
  adapter speed [khz]
        With an argument, change to the specified maximum jtag speed.  For
        JTAG, 0 KHz signifies adaptive clocking. With or without argument,
        display current setting. (command valid any time)
  adapter srst
        srst adapter command group (command valid any time)
    adapter srst delay [milliseconds]
          delay after deasserting SRST in ms (command valid any time)
    adapter srst pulse_width [milliseconds]
          SRST assertion pulse width in ms (command valid any time)
  adapter transports transport ...
        Declare transports the adapter supports. (configuration command)
  adapter usb
        usb adapter command group (command valid any time)
    adapter usb location [<bus>-port[.port]...]
          display or set the USB bus location of the USB device
          (configuration command)
add_help_text command_name helptext_string
      Add new command help text; Command can be multiple tokens. (command
      valid any time)
add_script_search_dir <directory>
      dir to search for config files and scripts (command valid any time)
add_usage_text command_name usage_string
      Add new command usage text; command can be multiple tokens. (command
      valid any time)
arm
      ARM Command Group (command valid any time)
  arm semihosting ['enable'|'disable']
        activate support for semihosting operations
  arm semihosting_basedir [dir]
        set the base directory for semihosting I/O operations
  arm semihosting_cmdline arguments
        command line arguments to be passed to program
  arm semihosting_fileio ['enable'|'disable']
        activate support for semihosting fileio operations
  arm semihosting_read_user_param
        read parameters in semihosting-user-cmd-0x10X callbacks
  arm semihosting_redirect (disable | tcp <port> ['debug'|'stdio'|'all'])
        redirect semihosting IO
  arm semihosting_resexit ['enable'|'disable']
        activate support for semihosting resumable exit
bindto [name]
      Specify address by name on which to listen for incoming TCP/IP
      connections (configuration command)
bp [<address> [<asid>] <length> ['hw'|'hw_ctx']]
      list or set hardware or software breakpoint
capture command
      Capture progress output and return as tcl return value. If the
      progress output was empty, return tcl return value. (command valid
      any time)
command
      core command group (introspection) (command valid any time)
  command mode [command_name ...]
        Returns the command modes allowed by a command: 'any', 'config', or
        'exec'. If no command is specified, returns the current command
        mode. Returns 'unknown' if an unknown command is given. Command can
        be multiple tokens. (command valid any time)
cti
      CTI commands (configuration command)
  cti create name '-chain-position' name [options ...]
        Creates a new CTI object (command valid any time)
  cti names
        Lists all registered CTI objects by name (command valid any time)
dap
      DAP commands (configuration command)
  dap create name '-chain-position' name
        Creates a new DAP instance (command valid any time)
  dap info [ap_num | 'root']
        display ROM table for specified MEM-AP (default MEM-AP of current
        target) or the ADIv6 root ROM table of current target's DAP
  dap init
        Initialize all registered DAP instances (command valid any time)
  dap names
        Lists all registered DAP instances by name (command valid any time)
debug_level number
      Sets the verbosity level of debugging output. 0 shows errors only; 1
      adds warnings; 2 (default) adds other info; 3 adds debugging; 4 adds
      extra verbose debugging. (command valid any time)
debug_reason
      displays the debug reason of this target
drscan tap_name (num_bits value)+ ['-endstate' state_name]
      Execute Data Register (DR) scan for one TAP.  Other TAPs must be in
      BYPASS mode.
dump_image filename address size
echo [-n] string
      Logs a message at "user" priority. Option "-n" suppresses trailing
      newline (command valid any time)
exit
      exit telnet session (command valid any time)
fast_load
      loads active fast load image to current target - mainly for profiling
      purposes
fast_load_image filename [address ['bin'|'ihex'|'elf'|'s19' [min_address
          [max_length]]]]
      Load image into server memory for later use by fast_load; primarily
      for profiling (command valid any time)
find <file>
      print full path to file according to OpenOCD search rules (command
      valid any time)
flash
      NOR flash command group (command valid any time)
  flash bank bank_id driver_name base_address size_bytes chip_width_bytes
            bus_width_bytes target [driver_options ...]
        Define a new bank with the given name, using the specified NOR
        flash driver. (configuration command)
  flash banks
        Display table with information about flash banks. (command valid
        any time)
  flash init
        Initialize flash devices. (configuration command)
  flash list
        Returns a list of details about the flash banks. (command valid any
        time)
flush_count
      Returns the number of times the JTAG queue has been flushed.
gdb
      GDB commands (command valid any time)
  gdb breakpoint_override ('hard'|'soft'|'disable')
        Display or specify type of breakpoint to be used by gdb 'break'
        commands. (command valid any time)
  gdb flash_program ('enable'|'disable')
        enable or disable flash program (configuration command)
  gdb memory_map ('enable'|'disable')
        enable or disable memory map (configuration command)
  gdb port [port_num]
        Normally gdb listens to a TCP/IP port. Each subsequent GDB server
        listens for the next port number after the base port number
        specified. No arguments reports GDB port. "pipe" means listen to
        stdin output to stdout, an integer is base port number, "disabled"
        disables port. Any other string is are interpreted as named pipe to
        listen to. Output pipe is the same name as input pipe, but with 'o'
        appended. (configuration command)
  gdb report_data_abort ('enable'|'disable')
        enable or disable reporting data aborts (configuration command)
  gdb report_register_access_error ('enable'|'disable')
        enable or disable reporting register access errors (configuration
        command)
  gdb save_tdesc
        Save the target description file
  gdb sync
        next stepi will return immediately allowing GDB to fetch register
        state without affecting target state (command valid any time)
  gdb target_description ('enable'|'disable')
        enable or disable target description (configuration command)
get_reg list
      Get register values from the target
halt [milliseconds]
      request target to halt, then wait up to the specified number of
      milliseconds (default 5000) for it to complete
help [command_name]
      Show full command help; command can be multiple tokens. (command
      valid any time)
init
      Initializes configured targets and servers.  Changes command mode
      from CONFIG to EXEC.  Unless 'noinit' is called, this command is
      called automatically at the end of startup. (command valid any time)
ipdbg
      IPDBG Hub/Host commands. (command valid any time)
  ipdbg create-hub name.ipdbghub (-tap device.tap -ir ir_value [dr_length]
            | -pld name.pld [user]) [-vir [vir_value [length
            [instr_code]]]]
        create a IPDBG Hub (command valid any time)
irscan [tap_name instruction]* ['-endstate' state_name]
      Execute Instruction Register (IR) scan.  The specified opcodes are
      put into each TAP's IR, and other TAPs are put in BYPASS.
jsp_port [port_num]
      Specify port on which to listen for incoming JSP telnet connections.
      (command valid any time)
jtag
      perform jtag tap actions (command valid any time)
  jtag arp_init
        Validates JTAG scan chain against the list of declared TAPs using
        just the four standard JTAG signals. (command valid any time)
  jtag arp_init-reset
        Uses TRST and SRST to try resetting everything on the JTAG scan
        chain, then performs 'jtag arp_init'. (command valid any time)
  jtag cget tap_name '-event' event_name | tap_name '-idcode'
        Return any Tcl handler for the specified TAP event or the value of
        the IDCODE found in hardware.
  jtag configure tap_name '-event' event_name handler
        Provide a Tcl handler for the specified TAP event. (command valid
        any time)
  jtag drscan tap_name (num_bits value)+ ['-endstate' state_name]
        Execute Data Register (DR) scan for one TAP.  Other TAPs must be in
        BYPASS mode.
  jtag flush_count
        Returns the number of times the JTAG queue has been flushed.
  jtag init
        initialize jtag scan chain (command valid any time)
  jtag names
        Returns list of all JTAG tap names. (command valid any time)
  jtag newtap basename tap_type '-irlen' count ['-enable'|'-disable']
            ['-expected_id' number] ['-ignore-version'] ['-ignore-bypass']
            ['-ircapture' number] ['-ir-bypass' number] ['-mask' number]
        Create a new TAP instance named basename.tap_type, and appends it
        to the scan chain. (configuration command)
  jtag pathmove start_state state1 [state2 [state3 ...]]
        Move JTAG state machine from current state (start_state) to state1,
        then state2, state3, etc.
  jtag tapdisable tap_name
        Try to disable the specified TAP using the 'tap-disable' TAP event.
  jtag tapenable tap_name
        Try to enable the specified TAP using the 'tap-enable' TAP event.
  jtag tapisenabled tap_name
        Returns a Tcl boolean (0/1) indicating whether the TAP is enabled
        (1) or not (0).
jtag_flush_queue_sleep [sleep in ms]
      For debug purposes(simulate long delays of interface) to test
      performance or change in behavior. Default 0ms. (command valid any
      time)
jtag_ntrst_assert_width [milliseconds]
      delay after asserting trst in ms (command valid any time)
jtag_ntrst_delay [milliseconds]
      delay after deasserting trst in ms (command valid any time)
jtag_rclk [fallback_speed_khz]
      With an argument, change to to use adaptive clocking if possible;
      else to use the fallback speed.  With or without argument, display
      current setting. (command valid any time)
load_image filename [address ['bin'|'ihex'|'elf'|'s19' [min_address
          [max_length]]]]
log_output [file_name | 'default']
      redirect logging to a file (default: stderr) (command valid any time)
mdb ['phys'] address [count]
      display memory bytes
mdd ['phys'] address [count]
      display memory double-words
mdh ['phys'] address [count]
      display memory half-words
mdw ['phys'] address [count]
      display memory words
measure_clk
      Runs a test to measure the JTAG clk. Useful with RCLK / RTCK.
      (command valid any time)
ms
      Returns ever increasing milliseconds. Used to calculate differences
      in time. (command valid any time)
mwb ['phys'] address value [count]
      write memory byte
mwd ['phys'] address value [count]
      write memory double-word
mwh ['phys'] address value [count]
      write memory half-word
mww ['phys'] address value [count]
      write memory word
nand
      NAND flash command group (command valid any time)
  nand device bank_id driver target [driver_options ...]
        defines a new NAND bank (configuration command)
  nand drivers
        lists available NAND drivers (command valid any time)
  nand init
        initialize NAND devices (configuration command)
noinit
      Prevent 'init' from being called at startup. (configuration command)
ocd_find file
      find full path to file (command valid any time)
pathmove start_state state1 [state2 [state3 ...]]
      Move JTAG state machine from current state (start_state) to state1,
      then state2, state3, etc.
pld
      programmable logic device commands (command valid any time)
  pld create name.pld driver_name [driver_args ... ]
        create a PLD device (configuration command)
  pld init
        initialize PLD devices (configuration command)
poll ['on'|'off']
      poll target state; or reconfigure background polling
poll_period
      set the servers polling period (command valid any time)
power_restore
      Overridable procedure run when power restore is detected. Runs 'reset
      init' by default. (command valid any time)
profile seconds filename [start end]
      profiling samples the CPU PC
program <filename> [address] [preverify] [verify] [reset] [exit]
      write an image to flash, address is only required for binary images.
      preverify, verify, reset, exit are optional (command valid any time)
ps
      list all tasks
rbp 'all' | address
      remove breakpoint
read_memory address width count ['phys']
      Read Tcl list of 8/16/32/64 bit numbers from target memory
reg [(register_number|register_name) [(value|'force')]]
      display (reread from target with "force") or set a register; with no
      arguments, displays all registers and their values
remote_bitbang
      perform remote_bitbang management (command valid any time)
  remote_bitbang host host_name
        Set the host to use to connect to the remote jtag.
  if port is 0
        or unset, this is the name of the unix socket to use.
        (configuration command)
  remote_bitbang port port_number
        Set the port to use to connect to the remote jtag.
  if 0 or unset,
        use unix sockets to connect to the remote jtag. (configuration
        command)
  remote_bitbang use_remote_sleep (on|off)
        Rather than executing sleep locally, include delays in the
        instruction stream for the remote host. (configuration command)
reset [run|halt|init]
      Reset all targets into the specified mode. Default reset mode is run,
      if not given.
reset_config [none|trst_only|srst_only|trst_and_srst]
          [srst_pulls_trst|trst_pulls_srst|combined|separate]
          [srst_gates_jtag|srst_nogate] [trst_push_pull|trst_open_drain]
          [srst_push_pull|srst_open_drain]
          [connect_deassert_srst|connect_assert_srst]
      configure adapter reset behavior (command valid any time)
reset_nag ['enable'|'disable']
      Nag after each reset about options that could have been enabled to
      improve performance. (command valid any time)
resume [address]
      resume target execution from current PC or address
riscv
      RISC-V Command Group (command valid any time)
  riscv authdata_read [index]
        Return the 32-bit value read from authdata or authdata0 (index=0),
        or authdata1 (index=1). (command valid any time)
  riscv authdata_write [index] value
        Write the 32-bit value to authdata or authdata0 (index=0), or
        authdata1 (index=1). (command valid any time)
  riscv dm_read reg_address
        Read and return 32-bit value from a debug module's register at
        reg_address. (command valid any time)
  riscv dm_write reg_address value
        Write a 32-bit value to the debug module's register at reg_address.
        (command valid any time)
  riscv dmi_read address
        Read and return 32-bit value from the given address on the RISC-V
        DMI bus. (command valid any time)
  riscv dmi_write address value
        Write a 32-bit value to the given address on the RISC-V DMI bus.
        (command valid any time)
  riscv dump_sample_buf [base64]
        Print the contents of the sample buffer, and clear the buffer.
        (command valid any time)
  riscv etrigger set [vs] [vu] [m] [s] [u] <exception_codes>|clear
        Set or clear a single exception trigger.
  riscv exec_progbuf instr1 [instr2 [... instr16]]
        Execute a sequence of 32-bit instructions using the program buffer.
        The final ebreak instruction is added automatically, if needed.
  riscv expose_csrs n0[-m0|=name0][,n1[-m1|=name1]]...
        Configure a list of inclusive ranges for CSRs to expose in addition
        to the standard ones. This must be executed before `init`.
        (configuration command)
  riscv expose_custom n0[-m0|=name0][,n1[-m1|=name1]]...
        Configure a list of inclusive ranges for custom registers to
        expose. custom0 is accessed as abstract register number 0xc000,
        etc. This must be executed before `init`. (configuration command)
  riscv hide_csrs {n0|n-m0}[,n1|n-m1]......
        Configure a list of inclusive ranges for CSRs to hide from gdb.
        Hidden registers are still available, but are not listed in gdb
        target description and `reg` command output. This must be executed
        before `init`. (configuration command)
  riscv icount set [vs] [vu] [m] [s] [u] [pending] <count>|clear
        Set or clear a single instruction count trigger.
  riscv info
        Displays some information OpenOCD detected about the target.
        (command valid any time)
  riscv itrigger set [vs] [vu] [nmi] [m] [s] [u] <mie_bits>|clear
        Set or clear a single interrupt trigger.
  riscv memory_sample bucket address|clear [size=4]
        Causes OpenOCD to frequently read size bytes at the given address.
        (command valid any time)
  riscv repeat_read count address [size=4]
        Repeatedly read the value at address. (command valid any time)
  riscv reset_delays [wait]
        OpenOCD learns how many Run-Test/Idle cycles are required between
        scans to avoid encountering the target being busy. This command
        resets those learned values after `wait` scans. It's only useful
        for testing OpenOCD itself. (command valid any time)
  riscv resume_order normal|reversed
        Choose the order that harts are resumed in when `hasel` is not
        supported. Normal order is from lowest hart index to highest.
        Reversed order is from highest hart index to lowest. (command valid
        any time)
  riscv set_bscan_tunnel_ir value
        Specify the JTAG TAP IR used to access the bscan tunnel. By default
        it is 0x23 << (ir_length - 6), which map some Xilinx FPGA (IR
        USER4) (command valid any time)
  riscv set_command_timeout_sec [sec]
        Set the wall-clock timeout (in seconds) for individual commands
        (command valid any time)
  riscv set_ebreakm [on|off]
        Control dcsr.ebreakm. When off, M-mode ebreak instructions don't
        trap to OpenOCD. Defaults to on. (command valid any time)
  riscv set_ebreaks [on|off]
        Control dcsr.ebreaks. When off, S-mode ebreak instructions don't
        trap to OpenOCD. Defaults to on. (command valid any time)
  riscv set_ebreaku [on|off]
        Control dcsr.ebreaku. When off, U-mode ebreak instructions don't
        trap to OpenOCD. Defaults to on. (command valid any time)
  riscv set_enable_trigger_feature [('eq'|'napot'|'ge_lt'|'all')
            ('wp'|'none')]
        Control whether OpenOCD is allowed to use certain RISC-V trigger
        features for watchpoints. (command valid any time)
  riscv set_enable_virt2phys on|off
        When on (default), enable translation from virtual address to
        physical address. (command valid any time)
  riscv set_enable_virtual on|off
        When on, memory accesses are performed on physical or virtual
        memory depending on the current system configuration. When off
        (default), all memory accessses are performed on physical memory.
        (command valid any time)
  riscv set_ir [idcode|dtmcs|dmi] value
        Set IR value for specified JTAG register. (command valid any time)
  riscv set_maskisr ['off'|'steponly']
        mask riscv interrupts
  riscv set_mem_access method1 [method2] [method3]
        Set which memory access methods shall be used and in which order of
        priority. Method can be one of: 'progbuf', 'sysbus' or 'abstract'.
        (command valid any time)
  riscv set_reset_timeout_sec [sec]
        DEPRECATED. Use 'riscv set_command_timeout_sec' instead. (command
        valid any time)
  riscv use_bscan_tunnel value [type]
        Enable or disable use of a BSCAN tunnel to reach DM.  Supply the
        width of the DM transport TAP's instruction register to enable. 
        Supply a value of 0 to disable. Pass A second argument (optional)
        to indicate Bscan Tunnel Type {0:(default) NESTED_TAP , 1:
        DATA_REGISTER} (command valid any time)
riscv.cpu0
      target command group (command valid any time)
  riscv.cpu0 arm
        ARM Command Group (command valid any time)
    riscv.cpu0 arm semihosting ['enable'|'disable']
          activate support for semihosting operations
    riscv.cpu0 arm semihosting_basedir [dir]
          set the base directory for semihosting I/O operations
    riscv.cpu0 arm semihosting_cmdline arguments
          command line arguments to be passed to program
    riscv.cpu0 arm semihosting_fileio ['enable'|'disable']
          activate support for semihosting fileio operations
    riscv.cpu0 arm semihosting_read_user_param
          read parameters in semihosting-user-cmd-0x10X callbacks
    riscv.cpu0 arm semihosting_redirect (disable | tcp <port>
              ['debug'|'stdio'|'all'])
          redirect semihosting IO
    riscv.cpu0 arm semihosting_resexit ['enable'|'disable']
          activate support for semihosting resumable exit
  riscv.cpu0 arp_examine ['allow-defer']
        used internally for reset processing
  riscv.cpu0 arp_halt
        used internally for reset processing
  riscv.cpu0 arp_halt_gdb
        used internally for reset processing to halt GDB
  riscv.cpu0 arp_poll
        used internally for reset processing
  riscv.cpu0 arp_reset 'assert'|'deassert' halt
        used internally for reset processing
  riscv.cpu0 arp_waitstate statename timeoutmsecs
        used internally for reset processing
  riscv.cpu0 cget target_attribute
        returns the specified target attribute (command valid any time)
  riscv.cpu0 configure [target_attribute ...]
        configure a new target for use (command valid any time)
  riscv.cpu0 curstate
        displays the current state of this target
  riscv.cpu0 debug_reason
        displays the debug reason of this target
  riscv.cpu0 eventlist
        displays a table of events defined for this target
  riscv.cpu0 examine_deferred
        used internally for reset processing
  riscv.cpu0 get_reg list
        Get register values from the target
  riscv.cpu0 invoke-event event_name
        invoke handler for specified event
  riscv.cpu0 mdb address [count]
        Display target memory as 8-bit bytes
  riscv.cpu0 mdd address [count]
        Display target memory as 64-bit words
  riscv.cpu0 mdh address [count]
        Display target memory as 16-bit half-words
  riscv.cpu0 mdw address [count]
        Display target memory as 32-bit words
  riscv.cpu0 mwb address data [count]
        Write byte(s) to target memory
  riscv.cpu0 mwd address data [count]
        Write 64-bit word(s) to target memory
  riscv.cpu0 mwh address data [count]
        Write 16-bit half-word(s) to target memory
  riscv.cpu0 mww address data [count]
        Write 32-bit word(s) to target memory
  riscv.cpu0 read_memory address width count ['phys']
        Read Tcl list of 8/16/32/64 bit numbers from target memory
  riscv.cpu0 riscv
        RISC-V Command Group (command valid any time)
    riscv.cpu0 riscv authdata_read [index]
          Return the 32-bit value read from authdata or authdata0
          (index=0), or authdata1 (index=1). (command valid any time)
    riscv.cpu0 riscv authdata_write [index] value
          Write the 32-bit value to authdata or authdata0 (index=0), or
          authdata1 (index=1). (command valid any time)
    riscv.cpu0 riscv dm_read reg_address
          Read and return 32-bit value from a debug module's register at
          reg_address. (command valid any time)
    riscv.cpu0 riscv dm_write reg_address value
          Write a 32-bit value to the debug module's register at
          reg_address. (command valid any time)
    riscv.cpu0 riscv dmi_read address
          Read and return 32-bit value from the given address on the RISC-V
          DMI bus. (command valid any time)
    riscv.cpu0 riscv dmi_write address value
          Write a 32-bit value to the given address on the RISC-V DMI bus.
          (command valid any time)
    riscv.cpu0 riscv dump_sample_buf [base64]
          Print the contents of the sample buffer, and clear the buffer.
          (command valid any time)
    riscv.cpu0 riscv etrigger set [vs] [vu] [m] [s] [u]
              <exception_codes>|clear
          Set or clear a single exception trigger.
    riscv.cpu0 riscv exec_progbuf instr1 [instr2 [... instr16]]
          Execute a sequence of 32-bit instructions using the program
          buffer. The final ebreak instruction is added automatically, if
          needed.
    riscv.cpu0 riscv expose_csrs n0[-m0|=name0][,n1[-m1|=name1]]...
          Configure a list of inclusive ranges for CSRs to expose in
          addition to the standard ones. This must be executed before
          `init`. (configuration command)
    riscv.cpu0 riscv expose_custom n0[-m0|=name0][,n1[-m1|=name1]]...
          Configure a list of inclusive ranges for custom registers to
          expose. custom0 is accessed as abstract register number 0xc000,
          etc. This must be executed before `init`. (configuration command)
    riscv.cpu0 riscv hide_csrs {n0|n-m0}[,n1|n-m1]......
          Configure a list of inclusive ranges for CSRs to hide from gdb.
          Hidden registers are still available, but are not listed in gdb
          target description and `reg` command output. This must be
          executed before `init`. (configuration command)
    riscv.cpu0 riscv icount set [vs] [vu] [m] [s] [u] [pending]
              <count>|clear
          Set or clear a single instruction count trigger.
    riscv.cpu0 riscv info
          Displays some information OpenOCD detected about the target.
          (command valid any time)
    riscv.cpu0 riscv itrigger set [vs] [vu] [nmi] [m] [s] [u]
              <mie_bits>|clear
          Set or clear a single interrupt trigger.
    riscv.cpu0 riscv memory_sample bucket address|clear [size=4]
          Causes OpenOCD to frequently read size bytes at the given
          address. (command valid any time)
    riscv.cpu0 riscv repeat_read count address [size=4]
          Repeatedly read the value at address. (command valid any time)
    riscv.cpu0 riscv reset_delays [wait]
          OpenOCD learns how many Run-Test/Idle cycles are required between
          scans to avoid encountering the target being busy. This command
          resets those learned values after `wait` scans. It's only useful
          for testing OpenOCD itself. (command valid any time)
    riscv.cpu0 riscv resume_order normal|reversed
          Choose the order that harts are resumed in when `hasel` is not
          supported. Normal order is from lowest hart index to highest.
          Reversed order is from highest hart index to lowest. (command
          valid any time)
    riscv.cpu0 riscv set_bscan_tunnel_ir value
          Specify the JTAG TAP IR used to access the bscan tunnel. By
          default it is 0x23 << (ir_length - 6), which map some Xilinx FPGA
          (IR USER4) (command valid any time)
    riscv.cpu0 riscv set_command_timeout_sec [sec]
          Set the wall-clock timeout (in seconds) for individual commands
          (command valid any time)
    riscv.cpu0 riscv set_ebreakm [on|off]
          Control dcsr.ebreakm. When off, M-mode ebreak instructions don't
          trap to OpenOCD. Defaults to on. (command valid any time)
    riscv.cpu0 riscv set_ebreaks [on|off]
          Control dcsr.ebreaks. When off, S-mode ebreak instructions don't
          trap to OpenOCD. Defaults to on. (command valid any time)
    riscv.cpu0 riscv set_ebreaku [on|off]
          Control dcsr.ebreaku. When off, U-mode ebreak instructions don't
          trap to OpenOCD. Defaults to on. (command valid any time)
    riscv.cpu0 riscv set_enable_trigger_feature
              [('eq'|'napot'|'ge_lt'|'all') ('wp'|'none')]
          Control whether OpenOCD is allowed to use certain RISC-V trigger
          features for watchpoints. (command valid any time)
    riscv.cpu0 riscv set_enable_virt2phys on|off
          When on (default), enable translation from virtual address to
          physical address. (command valid any time)
    riscv.cpu0 riscv set_enable_virtual on|off
          When on, memory accesses are performed on physical or virtual
          memory depending on the current system configuration. When off
          (default), all memory accessses are performed on physical memory.
          (command valid any time)
    riscv.cpu0 riscv set_ir [idcode|dtmcs|dmi] value
          Set IR value for specified JTAG register. (command valid any
          time)
    riscv.cpu0 riscv set_maskisr ['off'|'steponly']
          mask riscv interrupts
    riscv.cpu0 riscv set_mem_access method1 [method2] [method3]
          Set which memory access methods shall be used and in which order
          of priority. Method can be one of: 'progbuf', 'sysbus' or
          'abstract'. (command valid any time)
    riscv.cpu0 riscv set_reset_timeout_sec [sec]
          DEPRECATED. Use 'riscv set_command_timeout_sec' instead. (command
          valid any time)
    riscv.cpu0 riscv use_bscan_tunnel value [type]
          Enable or disable use of a BSCAN tunnel to reach DM.  Supply the
          width of the DM transport TAP's instruction register to enable. 
          Supply a value of 0 to disable. Pass A second argument (optional)
          to indicate Bscan Tunnel Type {0:(default) NESTED_TAP , 1:
          DATA_REGISTER} (command valid any time)
  riscv.cpu0 set_reg dict
        Set target register values
  riscv.cpu0 smp [on|off]
        smp handling
  riscv.cpu0 smp_gdb
        display/fix current core played to gdb
  riscv.cpu0 was_examined
        used internally for reset processing
  riscv.cpu0 write_memory address width data ['phys']
        Write Tcl list of 8/16/32/64 bit numbers to target memory
rtt
      RTT (command valid any time)
  rtt server
        RTT server (command valid any time)
    rtt server start <port> <channel> [message]
          Start a RTT server (command valid any time)
    rtt server stop <port>
          Stop a RTT server (command valid any time)
runtest num_cycles
      Move to Run-Test/Idle, and issue TCK for num_cycles.
rwp 'all' | address
      remove watchpoint
scan_chain
      print current scan chain configuration (command valid any time)
script <file>
      filename of OpenOCD script (tcl) to run (command valid any time)
set_reg dict
      Set target register values
shutdown
      shut the server down (command valid any time)
sleep milliseconds ['busy']
      Sleep for specified number of milliseconds.  "busy" will busy wait
      instead (avoid this). (command valid any time)
smp [on|off]
      smp handling
smp_gdb
      display/fix current core played to gdb
soft_reset_halt
      halt the target and do a soft reset
srst_deasserted
      Overridable procedure run when srst deassert is detected. Runs 'reset
      init' by default. (command valid any time)
step [address]
      step one instruction from current PC or address
svf [-tap device.tap] [-quiet] [-nil] [-progress] [-ignore_error]
          [-noreset] [-addcycles numcycles] file
      Runs a SVF file.
swo
      swo command group
  swo create name [-dap dap] [-ap-num num] [-baseaddr baseaddr]
        Creates a new TPIU or SWO object (command valid any time)
  swo init
        Initialize TPIU and SWO
  swo names
        Lists all registered TPIU and SWO objects by name (command valid
        any time)
target
      configure target (configuration command)
  target create name type '-chain-position' name [options ...]
        Creates and selects a new target (configuration command)
  target current
        Returns the currently selected target (command valid any time)
  target init
        initialize targets (configuration command)
  target names
        Returns the names of all targets as a list of strings (command
        valid any time)
  target smp targetname1 targetname2 ...
        gather several target in a smp list (command valid any time)
  target types
        Returns the available target types as a list of strings (command
        valid any time)
target_request
      target request command group (command valid any time)
  target_request debugmsgs ['enable'|'charmsg'|'disable']
        display and/or modify reception of debug messages from target
targets [target]
      change current default target (one parameter) or prints table of all
      targets (no parameters) (command valid any time)
tcl
      tcl command group (command valid any time)
  tcl notifications [on|off]
        Target Notification output
  tcl port [port_num]
        Specify port on which to listen for incoming Tcl syntax.  Read help
        on 'gdb port'. (configuration command)
  tcl trace [on|off]
        Target trace output
telnet_port [port_num]
      Specify port on which to listen for incoming telnet connections. 
      Read help on 'gdb port'. (configuration command)
test_image filename [offset [type]]
test_mem_access size
      Test the target's memory access functions
tms_sequence ['short'|'long']
      Display or change what style TMS sequences to use for JTAG state
      transitions:  short (default) or long.  Only for working around JTAG
      bugs. (command valid any time)
tpiu
      tpiu command group
  tpiu create name [-dap dap] [-ap-num num] [-baseaddr baseaddr]
        Creates a new TPIU or SWO object (command valid any time)
  tpiu init
        Initialize TPIU and SWO
  tpiu names
        Lists all registered TPIU and SWO objects by name (command valid
        any time)
trace
      trace command group
  trace history ['clear'|size]
        display trace history, clear history or set size
  trace point ['clear'|address]
        display trace points, clear list of trace points, or add new
        tracepoint at address
transport
      Transport command group (command valid any time)
  transport init
        Initialize this session's transport (command valid any time)
  transport list
        list all built-in transports (command valid any time)
  transport select [transport_name]
        Select this session's transport (command valid any time)
usage [command_name]
      Show basic command usage; command can be multiple tokens. (command
      valid any time)
verify_image filename [offset [type]]
verify_image_checksum filename [offset [type]]
verify_ircapture ['enable'|'disable']
      Display or assign flag controlling whether to verify values captured
      during Capture-IR. (command valid any time)
verify_jtag ['enable'|'disable']
      Display or assign flag controlling whether to verify values captured
      during IR and DR scans. (command valid any time)
version [git]
      show program version (command valid any time)
virt2phys virtual_address
      translate a virtual address into a physical address (command valid
      any time)
wait_halt [milliseconds]
      wait up to the specified number of milliseconds (default 5000) for a
      previously requested halt
wait_srst_deassert ms
      Wait for an SRST deassert. Useful for cases where you need something
      to happen within ms of an srst deassert. Timeout in ms (command valid
      any time)
wp [address length [('r'|'w'|'a') [value [mask]]]]
      list (no params) or create watchpoints
write_memory address width data ['phys']
      Write Tcl list of 8/16/32/64 bit numbers to target memory
xsvf (tapname|'plain') filename ['virt2'] ['quiet']
      Runs a XSVF file.  If 'virt2' is given, xruntest counts are
      interpreted as TCK cycles rather than as microseconds.  Without the
      'quiet' option, all comments, retries, and mismatches will be
      reported.
```






## GCC toolchain for riscv

### Building from source

https://github.com/riscv-collab/riscv-gnu-toolchain
https://stackoverflow.com/questions/74231514/how-to-install-riscv32-unknown-elf-gcc-on-debian-based-linuxes

```
sudo apt-get install autoconf automake autotools-dev curl python3 libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev ninja-build

sudo apt-get install autoconf automake autotools-dev curl python3 python3-pip libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev ninja-build git cmake libglib2.0-dev libslirp-dev
```

Supported ABIs are 
ilp32 (32-bit soft-float), 
ilp32d (32-bit hard-float), 
ilp32f (32-bit with single-precision in registers and double in memory, niche use only), 
lp64 
lp64f 
lp64d (same but with 64-bit long and pointers).

```
mkdir ~/dev
cd dev
mkdir riscv-toolchain
cd riscv-toolchain

git clone https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain

%% This toolchain causes the error: bfd requires flen 8, but target has flen 0 
%% when debugging the executable with gdb. This error goes away, when from the misa register, 
%% the RISC-V CPU returns that it supports the F and D extensions.
%%
%% It seems as if the gdb contained in the repository only works with riscv cpus that
%% support floating point in one way or another.
%%
%% How does gdb talk to openocd and openocd to the CPU to figure out that the 
%% CPU has no floating point support? How does it work?
./configure --prefix=/opt/riscv --with-arch=rv32gc --with-abi=ilp32d

%% Use this command for float (Combats error: bfd requires flen 8, but target has flen 0, see 
%% https://github.com/riscv-software-src/riscv-isa-sim/issues/380)
%% This error goes away, when from the misa register, 
%% the RISC-V CPU returns that it supports the F and D extensions.
%%
%% Warning: this will fail with: 
%% In file included from 
%% /home/wbi/dev/riscv-toolchain/riscv-gnu-toolchain/build-gcc-linux-stage2/gcc/include-fixed/pthread.h:36,
%%                  from ./gthr-default.h:35,
%%                  from ../../.././gcc/libgcc/gthr.h:148,
%%                  from ../../.././gcc/libgcc/libgcov-interface.c:27:
%% /opt/riscv/sysroot/usr/include/bits/setjmp.h:35:3: error: #error unsupported FLEN
%%    35 | # error unsupported FLEN
%%       |   ^~~~~
./configure --prefix=/opt/riscv --with-arch=rv32if --with-abi=ilp32f

%% use this for no floating point hardware
%%
%% Warning: this will fail with:
%% In file included from /opt/riscv/sysroot/usr/include/features.h:535,
%%                  from /opt/riscv/sysroot/usr/include/bits/libc-header-start.h:33,
%%                  from /opt/riscv/sysroot/usr/include/stdio.h:28,
%%                  from ../../.././gcc/libgcc/../gcc/tsystem.h:87,
%%                  from ../../.././gcc/libgcc/libgcc2.c:27:
%% /opt/riscv/sysroot/usr/include/gnu/stubs.h:8:11: fatal error: gnu/stubs-ilp32.h: No such file or directory
%%     8 | # include <gnu/stubs-ilp32.h>
./configure --prefix=/opt/riscv --with-arch=rv32i --with-abi=ilp32

sudo make linux
```

--with-arch=rv32imcf,

--with-arch=rv64imac

M für Ganzzahl-Multiplikation und -Division
A für atomare Speicheroperationen in Multiprozessorsystemen
F, D und Q für Gleitkommaoperationen nach IEEE 754: binary32, binary64 bzw. binary128
13 “D” Standard Extension for Double-Precision Floating-Point, Version 2.2
Zicsr für Control and Status Registers (CSRs) (siehe unten)
C für komprimierte Befehle, das sind 16-bit-Kodierungen häufig verwendeter Befehle, die somit den Speicherverbrauch reduzieren.

See: https://github.com/martinKindall/compile-for-risc-v-gnu

Example C-Code (example.c):

```
int main() {

    int anumber = 5;
    int another = 30;

    int result = anumber + another;

    return 0;
}
```

https://stackoverflow.com/questions/5244509/no-debugging-symbols-found-when-using-gdb
For debug symbols: The application has to be both compiled and linked with -g option. I.e. you need to put -g in both CPPFLAGS and LDFLAGS.

```
rm example.elf example.bin example.hex example.o
/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32id -g -r example.c -o example.o
/opt/riscv/bin/riscv32-unknown-linux-gnu-ld -g -o example.elf -T linker_script.ld -m elf32lriscv -nostdlib --no-relax
```

To make debug symbols work in gdb, use this:

```
cd ~/dev/openocd/riscv_openocd_jtag_mock/create_binary_example
rm example.elf example.bin example.hex example.o a.out

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32id -fverbose-asm -g example.c -o example.elf

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -v -march=rv32id -mabi=ilp32 -nostartfiles -x c -Ttext 40000000 -Tdata 50000000 -Tbss 60000000 -o example.o example.c

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -v -march=rv32id -nostartfiles -x c -Ttext 40000000 -Tdata 50000000 -Tbss 60000000 -o example.elf example.c

Inspect the .elf file: 
/opt/riscv/bin/riscv32-unknown-linux-gnu-objdump -s -j .text /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32i -g example.c -o example.elf

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32i -mabi=ilp32 -g example.c -o example.elf
/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32im -mabi=ilp32 -g example.c -o example.elf
/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -march=rv32imd -g example.c -o example.elf
```

/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -E -fdirectives-only -o MySrc.lst example.c

This works: Create a assembly listing (see https://www.systutorials.com/generate-a-mixed-source-and-assembly-listing-using-gcc/)
```
/opt/riscv/bin/riscv32-unknown-linux-gnu-gcc -Wa,-adhln -g example.c > assembly_list.s
```

Convert elf to bin using riscv32-unknown-linux-gnu-objcopy
```
/opt/riscv/bin/riscv32-unknown-linux-gnu-objcopy -O binary example.elf example.bin
```

Convert elf to ihex using riscv32-unknown-linux-gnu-objcopy (this is a valid ihex format file!)
```
/opt/riscv/bin/riscv32-unknown-linux-gnu-objcopy -O ihex example.elf example.hex
```

Convert bin to a hexdump (this is not the ihex format!)
``` 
hexdump -e '"%08x\n"' example.bin > example.hex
```

Convert elf to hex using riscv32-unknown-elf-elf2hex (might not be available without explicit installation)
``` 
riscv32-unknown-elf-elf2hex --bit-width 32 --input example.elf --output example.hex
```

upload / Load hex file with openocd into the target.
The <offset> can be used to load all the hex file sections to their original addresses plus the offset value.
To just use the original addresses, specify an offset of 0x00000000.
The original addresses are either specified by parameters to gcc or by using a linker and a linker-script
which defines which section to place where.

```
load_image <path_to_ihex_file> <offset> <format>
load_image create_binary_example/example.hex 0x00000000 ihex
```

Write a single memory cell
```
write_memory address width data
write_memory 0x01 32 0x22
```

```
riscv info
```

Error: 
```
Warn : [riscv.cpu0] Buggy aampostincrement! Address not incremented correctly.
```
The solution is to increment arg1 as the specification says.



openocd log output is

```
Debug: 20189 35864 command.c:153 script_debug(): command - load_image create_binary_example/example.hex 0x00000000 ihex
Debug: 20190 35864 configuration.c:88 find_file(): found create_binary_example/example.hex
Debug: 20191 176208 target.c:2350 target_write_buffer(): writing buffer of 229 byte at 0x00000000
```

The access_memory_command() will first write the target address into 
data3 data3 data2 data2



### Install prebuild binaries gcc-riscv64-linux-gnu

https://www.youtube.com/watch?v=GWiAQs4-UQ0

sudo apt install gcc-riscv64-linux-gnu





## How to display a .elf file

/opt/riscv/bin/riscv32-unknown-linux-gnu-readelf -a /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf

objdump -s -j .text /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf
objdump -s -j .rodata /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf


## GNU Debugger (gdb)

Start gdb:

```
/opt/riscv/bin/riscv32-unknown-linux-gnu-gdb /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf
```

Connect gdb to openocd:

```
target remote localhost:3333
target extended-remote localhost:3333
```

Query registers

```
info registers
```

Stop gdb:

```
quit
```

```
gdb /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf
```

## Architecture

set arch riscv:rv32

## OpenOCD

target extended-remote localhost:3333

## File:

file <program to be debugged> - select the file to debug
example:
file /home/wbi/dev/openocd/riscv_openocd_jtag_mock/create_binary_example/example.elf

## Breakpoints:

info breakpoints    - lists all current breakpoints
break main          - set a breakpoint in the main function
b main              - set a breakpoint in the main function
b *0x00000000       - set a breakpoint at memory location 0x00000000

## Single Stepping

stepi

## Executing

run
continue

## ni (next instruction)

ni

## Show SourceCode

list

## Show disassembled source code



bfd requires flen 8, but target has flen 0
bfd requires flen 8, but target has flen 4

%% This error goes away, when from the misa register, 
%% the RISC-V CPU returns that it supports the F and D extensions.

https://sourceware.org/pipermail/gdb-cvs/2019-January/044597.html

if (abi_features.flen > features.flen)
     error (_("bfd requires flen %d, but target has flen %d"),
-           info_features.flen, features.flen);


gdb uses the BFD subroutine library to examine multiple object-file formats; BFD was
a joint project of David V. Henkel-Wallace, Rich Pixley, Steve Chamberlain, and John
Gilmore.



bfd requires flen 8, but target has flen 0
bfd requires flen 8, but target has flen 4

%% This error goes away, when from the misa register, 
%% the RISC-V CPU returns that it supports the F and D extensions.

https://github.com/riscv-software-src/riscv-isa-sim/issues/380

For anyone else struggling with this error message (as I have for 2 days), note that regardless of the GCC options used to compile your code, the parameters used to build the toolchain may override the settings. When configuring the toolchain, use this for 4-byte (single precision) floats:

./configure --prefix=/opt/riscv --with-arch=rv32if --with-abi=ilp32f

or

./configure --prefix=/opt/riscv --with-arch=rv32i --with-abi=ilp32

for no hard floats, matching the target flen of zero.