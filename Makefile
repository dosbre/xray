include config.mk

OBJECTS = xray.o window.o util.o event.o

all: xray

xray: ${OBJECTS}

${OBJECTS}: xray.h

clean:
	@echo cleaning
	@rm -f xray ${OBJECTS}

install: xray
	@echo installing executable file to ${PREFIX}/bin
	@mkdir -p ${PREFIX}/bin
	@cp -f xray ${PREFIX}/bin

uninstall:
	@echo removing executable file from ${PREFIX}/bin
	@rm ${PREFIX}/bin/xray

.PHONY: all clean install uninstall

