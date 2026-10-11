CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -g -Wall -Wextra
#CFLAGS += -Wpedantic -Werror

TARGET = writer 
SRC = writer.c

.PHONY: all build clean
all build: $(TARGET)

$(TARGET): $(SRC) 
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
clean:
	rm -f *.o $(TARGET)
