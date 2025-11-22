CC := arm-linux-gnueabihf-gcc

all:
	$(CC) -static -O2 -o move_image_file move_image_file.c -lm

clean:
	rm -f move_image_file
