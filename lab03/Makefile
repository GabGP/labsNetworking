## Laboratorio 3 - Servidor DNS sobre UDP (implementacion en C)
##
## Objetivos principales:
##   make          Compila el servidor DNS  -> ./dns_server
##   make run      Compila y ejecuta con sudo en el puerto 53
##   make test     Ejecuta en el puerto 5353 (sin root) para pruebas
##   make clean    Borra binarios y objetos
##
## Objetivos heredados del material base del laboratorio:
##   make udp_test / make runudp / make runjava / make killudp

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11 -D_DEFAULT_SOURCE -Isrc
LDFLAGS  = -pthread

SRCDIR   = src
OBJDIR   = build
TARGET   = dns_server

SOURCES  = $(SRCDIR)/main.c        \
           $(SRCDIR)/dns_server.c  \
           $(SRCDIR)/dns_parser.c  \
           $(SRCDIR)/dns_records.c \
           $(SRCDIR)/dns_cache.c   \
           $(SRCDIR)/threadpool.c  \
           $(SRCDIR)/log.c

OBJECTS  = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Material base entregado con el laboratorio
LEGACY_SRC = lab_3.c
LEGACY_OUT = udp_test

.PHONY: all run test clean distclean runudp runjava killudp help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "  Compilado: ./$(TARGET)"
	@echo "  Ejecute 'make run' (puerto 53, requiere sudo)"
	@echo "  o 'make test' (puerto 5353, sin privilegios)."
	@echo ""

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Dependencias de cabeceras: si cambia un .h se recompila todo.
$(OBJECTS): $(wildcard $(SRCDIR)/*.h)

## El puerto 53 es privilegiado: se necesita sudo.
run: $(TARGET)
	sudo ./$(TARGET)

## Pruebas sin root; verificar con: dig @127.0.0.1 -p 5353 www.lab.local A
test: $(TARGET)
	./$(TARGET) -p 5353 -v

clean:
	rm -rf $(OBJDIR) $(TARGET) $(LEGACY_OUT)

distclean: clean
	rm -f dns_server.log

## ------------------------------------------------------------------
## Material base original del laboratorio
## ------------------------------------------------------------------

$(LEGACY_OUT): $(LEGACY_SRC)
	$(CC) -O2 -Wall -Wextra -o $(LEGACY_OUT) $(LEGACY_SRC)

runudp: $(LEGACY_OUT)
	./$(LEGACY_OUT)

runjava:
	javac UdpBroadcastSender.java && java UdpBroadcastSender

## Libera el puerto UDP 9999 del ejemplo base (falla si nadie lo ocupa).
killudp:
	sudo kill -9 $$(sudo lsof -t -iUDP:9999)

## Libera el puerto 53 si systemd-resolved lo tiene tomado.
free53:
	sudo systemctl stop systemd-resolved || true
	sudo lsof -i UDP:53 || true

help:
	@echo "Objetivos disponibles:"
	@echo "  make          Compila ./$(TARGET)"
	@echo "  make run      Ejecuta con sudo en UDP:53"
	@echo "  make test     Ejecuta en UDP:5353 con log DEBUG"
	@echo "  make clean    Borra binarios y objetos"
	@echo "  make free53   Detiene systemd-resolved y muestra quien usa el 53"
	@echo "  make runudp   Compila y ejecuta el ejemplo base lab_3.c"
	@echo "  make runjava  Ejecuta UdpBroadcastSender.java"
	@echo "  make killudp  Libera el puerto UDP 9999"
