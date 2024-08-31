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
``` 

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
