VERSION = "0.9"
DEBUG = 

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic ${DEBUG} -DVERSION=${VERSION}
LDFLAGS = -lxcb -lxcb-render-util -lxcb-render -lxcb-xfixes \
	  -lxcb-damage -lxcb-composite

