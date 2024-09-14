.PHONY: all
all: a.out

a.out: remote_bitbang_main.cpp \
	remote_bitbang.h remote_bitbang.cpp \
	tap_state_machine.h tap_state_machine.cpp \
	tap_state_machine_callback.h tap_state_machine_callback.cpp \
	ihex_loader.h ihex_loader.cpp
	g++ -g remote_bitbang_main.cpp \
	remote_bitbang.cpp \
	tap_state_machine.cpp \
	tap_state_machine_callback.cpp \
	ihex_loader.cpp

.PHONY: clean
clean:
	rm *.o a.out
