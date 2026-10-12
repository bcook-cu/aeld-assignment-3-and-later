CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -g -Wall -Wextra
CFLAGS += -Wpedantic
CFLAGS += -Werror

TARGET = aesdsocket 
SRC = aesdsocket.c

.PHONY: all default clean
all default: $(TARGET)

$(TARGET): $(SRC) 
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
clean:
	rm -f *.o $(TARGET)
