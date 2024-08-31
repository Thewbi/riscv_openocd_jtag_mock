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

Now start openocd using the configuration file

```
cd /home/<USERNAME>/dev/openocd/riscv_openocd_jtag_mock
set JTAG_VPI_PORT=36054;
set JTAG_DTM_ENABLE_SBA=on; 
/home/<USERNAME>/openocd/bin/openocd -f remote_bitbang.cfg -d -l log
```

```
cd /home/wbi/dev/openocd/riscv_openocd_jtag_mock
set JTAG_VPI_PORT=36054;
set JTAG_DTM_ENABLE_SBA=on;
/home/wbi/openocd/bin/openocd -f remote_bitbang.cfg -d -l log
```

openocd will not print to the console since the -l parameter has been passed.
Instead openocd will write into the a file called log as specified on the 
command line.

When openocd is run like this right now, it will fail to connect to the
server on localhost:3335 since this server does not exist yet!

# Create the server (= mocked target)

```
g++ remote_bitbang_main.cpp remote_bitbang.cpp -g
./a.out
/home/wbi/openocd/bin/openocd -f remote_bitbang.cfg -d -l log
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

# Resetting the TAP's statemachine to the Test-Logic-Reset state

TAP is the Test Access Port which is the module that the JTAG client (openocd) will talk to. The TAP is the
device that terminates the JTAG connection coming in from the outside. It will interpret the commands 
and tranlate them to the necessary interal implementation to cause the system to perform actions.

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

Looking at the commands which openocd actually sends, there are more than 5 toggles but this
really does not matter. The TAP state is Test-Logic-Reset in any case.



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
not elements in the chain. This is a dumb example since there must be at least one
element in the chain.

If the client sends in a 1 and reads a 0 and then reads a 1, there is a delay of 1 
and hence there is a single element in the chain.

If the client sends in a 1 and reads four 0 before reading a 1, then there are 4
elements in the chain.

# Move the TAP's state machine to shift-IR state

Therefore it sends 