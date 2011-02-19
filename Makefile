CC = gcc

CFLAGS = -std=c89 -Wall -pedantic

LDFLAGS = -lxcb -lxcb-damage -lxcb-xfixes -lxcb-composite

TARGET = xray

all: $(TARGET)

$(TARGET): xray.o

xray.o: xray.c xray.h

clean:
	@echo cleaning
	@rm -f $(TARGET) xray.o

.PHONY: all clean

