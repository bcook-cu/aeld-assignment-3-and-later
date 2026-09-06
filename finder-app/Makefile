CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -g -Wall -Wextra
#CFLAGS += -Wpedantic -Werror

TARGET = writer 
SRC = writer.c

build: $(SRC) 
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
clean:
	rm -f *.o $(TARGET)
