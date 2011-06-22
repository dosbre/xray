VERSION = 0.7

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -DDEBUG
LDFLAGS = -lxcb -lxcb-render-util -lxcb-render -lxcb-xfixes \
	  -lxcb-damage -lxcb-composite

