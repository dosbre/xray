include config.mk

all: xray
	@mkdir -p ./bin
	@mv xray ./bin

xray: xray.o util.o window.o event.o

xray.o: xray.h

clean:
	@echo cleaning
	@rm -f ./bin/xray xray.o event.o util.o
	@rmdir ./bin

install: all
	@echo installing executable file to ${PREFIX}/bin
	@mkdir -p ${PREFIX}/bin
	@cp -f ./bin/xray ${PREFIX}/bin

uninstall:
	@echo removing executable file from ${PREFIX}/bin
	@rm ${PREFIX}/bin/xray

.PHONY: all clean install uninstall

