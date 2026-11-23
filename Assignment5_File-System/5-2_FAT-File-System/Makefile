CC = gcc
CFLAGS = -Wall -Wextra -g

TARGET = fat

all: $(TARGET)

$(TARGET): fat.c
	$(CC) $(CFLAGS) -o $(TARGET) fat.c

clean:
	rm -f $(TARGET) fs_image.bin

