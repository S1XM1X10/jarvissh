CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = mysh

SRCS = main.c arraylist.c

all: main.c arraylist.c arraylist.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -rf mysh mysh.dSYM/
