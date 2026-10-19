# =============================================================================
#  Lab09 - SpeedTest: TCP "a puro pulmón" con sockets RAW (Linux)
#
#  make          genera payload.bin (1000 bytes) y compila en modo release
#  make demo     servidor + cliente en esta misma máquina (127.0.0.1)
#  make help     lista de objetivos y variables
# =============================================================================

# Variables configurables, por ejemplo: make run-client SERVER=192.168.1.5 PORT=5030
SERVER  ?= 127.0.0.1
PORT    ?= 8080
SPORT   ?= 30000
PING    ?= icmp
PINGS   ?= 4
PAYLOAD ?= payload.bin
IFACE   ?= lo
PCAP    ?= speedtest.pcap
ZIP     ?= lab09_speedtest.zip

CLIENT_BIN := target/release/client
SERVER_BIN := target/release/server
PROJECT    := $(notdir $(CURDIR))

# El kernel no sabe que el cliente maneja el puerto SPORT por su cuenta: al llegar
# el SYN-ACK respondería con un RST que mata la conexión. Esta regla lo descarta.
RST_RULE := OUTPUT -p tcp --tcp-flags RST RST --sport $(SPORT) -j DROP
RST_ADD   = sudo iptables -C $(RST_RULE) 2>/dev/null || sudo iptables -A $(RST_RULE)
RST_DEL   = while sudo iptables -D $(RST_RULE) 2>/dev/null; do :; done

RUN_CLIENT   = sudo ./$(CLIENT_BIN) server $(SERVER) port $(PORT) sport $(SPORT) file $(PAYLOAD) ping $(PING) pings $(PINGS)
START_SERVER = sudo ./$(SERVER_BIN) port $(PORT) < /dev/null > server.log 2>&1 & server_pid=$$!
STOP_SERVER  = if [ -n "$$server_pid" ]; then sudo kill $$server_pid 2>/dev/null; fi
SHOW_LOG     = sleep 0.5; echo; echo "==================== Log del servidor (server.log) ===================="; cat server.log

.PHONY: all help deps payload build test rst-block rst-unblock run-server run-client demo capture zip clean

all: payload build

help:
	@echo "Objetivos:"
	@echo "  make / make all   genera $(PAYLOAD) y compila (cargo build --release)"
	@echo "  make payload      genera $(PAYLOAD) con 1000 bytes aleatorios si no existe"
	@echo "  make build        compila el cliente y el servidor (cargo build --release)"
	@echo "  make test         pruebas unitarias (cargo test, no requiere sudo)"
	@echo "  make deps         verifica cargo, perl + Net::Ping, iptables, tcpdump y zip"
	@echo "  make run-server   ejecuta el servidor con sudo en el puerto $(PORT)"
	@echo "  make run-client   agrega la regla de iptables, ejecuta el cliente con sudo y la elimina"
	@echo "  make demo         servidor + cliente en 127.0.0.1 y luego muestra el log del servidor"
	@echo "  make capture      igual que demo, pero guarda los segmentos en $(PCAP) para Wireshark"
	@echo "  make rst-block    solo agrega la regla de iptables (puerto origen $(SPORT))"
	@echo "  make rst-unblock  solo elimina la regla de iptables"
	@echo "  make zip          empaqueta el proyecto en $(ZIP) para la entrega"
	@echo "  make clean        borra target/, $(PAYLOAD) y los archivos generados"
	@echo ""
	@echo "Variables: SERVER=$(SERVER) PORT=$(PORT) SPORT=$(SPORT) PING=$(PING) PINGS=$(PINGS) IFACE=$(IFACE)"

deps:
	@command -v cargo >/dev/null && cargo --version || echo "FALTA cargo: instale Rust desde https://rustup.rs"
	@perl -MNet::Ping -e 'printf "perl %vd, Net::Ping %s\n", $$^V, $$Net::Ping::VERSION' || echo "FALTA perl o el módulo Net::Ping"
	@PATH="$$PATH:/usr/sbin:/sbin" command -v iptables >/dev/null && echo "iptables: ok" || echo "FALTA iptables (sudo apt install iptables)"
	@PATH="$$PATH:/usr/sbin:/sbin" command -v tcpdump >/dev/null && echo "tcpdump: ok" || echo "(opcional) tcpdump no está instalado"
	@command -v zip >/dev/null && echo "zip: ok" || echo "FALTA zip (sudo apt install zip)"

payload: $(PAYLOAD)

$(PAYLOAD):
	head -c 1000 /dev/urandom > $@
	@echo "$@ generado: $$(wc -c < $@) bytes"

build:
	cargo build --release

test:
	cargo test

rst-block:
	@$(RST_ADD)
	@echo "iptables: regla activa ($(RST_RULE))"

rst-unblock:
	@$(RST_DEL)
	@echo "iptables: regla eliminada"

run-server: build
	sudo ./$(SERVER_BIN) port $(PORT)

# La regla se elimina al terminar el cliente, aunque falle o se interrumpa con Ctrl+C.
run-client: all
	@$(RST_ADD)
	@echo "iptables: regla activa ($(RST_RULE))"
	@trap '$(RST_DEL); echo "iptables: regla eliminada"' EXIT; trap 'exit 130' INT TERM; \
	$(RUN_CLIENT)

# Servidor en segundo plano (su salida va a server.log) + cliente contra 127.0.0.1.
demo: override SERVER = 127.0.0.1
demo: all
	@sudo -v
	@trap '$(RST_DEL); $(STOP_SERVER)' EXIT; trap 'exit 130' INT TERM; \
	$(START_SERVER); sleep 1; \
	$(RST_ADD) || exit 1; \
	$(RUN_CLIENT); status=$$?; \
	$(SHOW_LOG); exit $$status

# Igual que demo, capturando con tcpdump los segmentos del puerto PORT en la interfaz IFACE.
capture: override SERVER = 127.0.0.1
capture: all
	@sudo -v
	@trap '$(RST_DEL); $(STOP_SERVER); if [ -n "$$tcpdump_pid" ]; then sudo kill $$tcpdump_pid 2>/dev/null; fi' EXIT; \
	trap 'exit 130' INT TERM; \
	sudo tcpdump -i $(IFACE) -nn -U -w - 'tcp port $(PORT)' < /dev/null > $(PCAP) 2> tcpdump.log & tcpdump_pid=$$!; \
	$(START_SERVER); sleep 1; \
	$(RST_ADD) || exit 1; \
	$(RUN_CLIENT); status=$$?; \
	sleep 1; sudo kill $$tcpdump_pid 2>/dev/null; wait $$tcpdump_pid; tcpdump_pid=; \
	$(SHOW_LOG); \
	echo; echo "==================== Segmentos capturados ($(PCAP)) ===================="; \
	tcpdump -nn -r $(PCAP) 2>/dev/null || cat tcpdump.log; \
	echo; echo "Abra $(PCAP) en Wireshark para ver cada encabezado en detalle."; exit $$status

# El .zip contiene la carpeta del proyecto sin target/ ni los archivos generados al probar.
zip: payload
	@rm -f "$(ZIP)"
	cd .. && zip -r "$(CURDIR)/$(ZIP)" "$(PROJECT)" -x "$(PROJECT)/target/*" "*.zip" "*.pcap" "*.log"
	@echo "Entrega lista: $(ZIP)"

clean:
	cargo clean
	rm -f $(PAYLOAD) $(ZIP) $(PCAP) server.log tcpdump.log
