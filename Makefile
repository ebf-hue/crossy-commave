CC_BB  := arm-linux-gnueabihf-gcc
CC_PC  := gcc
SRC    := main.c declarations.c platform.c
EXEC   := sprite_test

all: laptop

beaglebone:
	$(CC_BB) -static -O2 -o $(EXEC) $(SRC) -lm

laptop:
	$(CC_PC) $(SRC) -o $(EXEC) -DUSE_SDL `sdl2-config --cflags --libs` -lm

clean:
	rm -f $(EXEC)

