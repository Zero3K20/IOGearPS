# Top-level Makefile for the IOGear GPSU21 FreeRTOS firmware build.
#
# Prerequisites (set via make variables):
#   FREERTOS_DIR   - path to extracted FreeRTOS-Kernel source tree
#   LWIP_DIR       - path to extracted lwIP source tree
#   CROSS_COMPILE  - toolchain prefix (default: mipsel-linux-gnu-)

CROSS_COMPILE ?= mipsel-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
AS      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy

TOP_DIR     := $(CURDIR)
BUILD_DIR   := $(TOP_DIR)/build
SRC_DIR     := $(TOP_DIR)/src
BSP_DIR     := $(TOP_DIR)/bsp
FRT_PORT    := $(BSP_DIR)/freertos_port
LWIP_PORT   := $(TOP_DIR)/freertos/lwip_port
NETIF_DIR   := $(TOP_DIR)/freertos/netif
WEB_DIR     := $(TOP_DIR)/../gpsu21_web
PKG_SCRIPT  := $(TOP_DIR)/package_firmware.py

ELF_OUT     := $(BUILD_DIR)/gpsu21_app.elf
BIN_OUT     := $(BUILD_DIR)/gpsu21_app.bin
FW_OUT      := $(BUILD_DIR)/gpsu21_freertos.bin

FREERTOS_DIR ?= $(TOP_DIR)/../freertos-kernel
FRT_SRC      := $(FREERTOS_DIR)
FRT_INC      := $(FRT_SRC)/include

LWIP_DIR  ?= $(TOP_DIR)/../lwip
LWIP_SRC  := $(LWIP_DIR)/src

CFLAGS := \
	-mips32r2 \
	-EL \
	-march=mips32r2 \
	-msoft-float \
	-G 0 \
	-fno-builtin \
	-ffunction-sections \
	-fdata-sections \
	-Os \
	-g \
	-Wall \
	-Wextra \
	-Wno-unused-parameter \
	-ffreestanding \
	-I$(SRC_DIR) \
	-I$(BSP_DIR) \
	-I$(FRT_PORT) \
	-I$(TOP_DIR)/freertos \
	-I$(LWIP_PORT) \
	-I$(FRT_INC) \
	-I$(LWIP_SRC)/include \
	-DCYG_HAL_STARTUP_ROM \
	-DRAM_STARTUP \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=0

ASFLAGS := \
	$(CFLAGS) \
	-mno-abicalls \
	-fno-pic \
	-x assembler-with-cpp

LDFLAGS := \
	-EL \
	-mips32r2 \
	-msoft-float \
	-G 0 \
	-nostartfiles \
	-nostdlib \
	-T$(BSP_DIR)/linker.ld \
	-Wl,--gc-sections \
	-Wl,-Map=$(BUILD_DIR)/gpsu21_app.map \
	-Wl,--build-id=none \
	-lc \
	-lgcc

FRT_OBJS := \
	$(BUILD_DIR)/tasks.o \
	$(BUILD_DIR)/list.o \
	$(BUILD_DIR)/queue.o \
	$(BUILD_DIR)/timers.o \
	$(BUILD_DIR)/event_groups.o \
	$(BUILD_DIR)/stream_buffer.o \
	$(BUILD_DIR)/heap_4.o \
	$(BUILD_DIR)/port.o \
	$(BUILD_DIR)/port_asm.o

LWIP_CORE_SRCS := \
	$(LWIP_SRC)/core/init.c \
	$(LWIP_SRC)/core/def.c \
	$(LWIP_SRC)/core/dns.c \
	$(LWIP_SRC)/core/inet_chksum.c \
	$(LWIP_SRC)/core/ip.c \
	$(LWIP_SRC)/core/mem.c \
	$(LWIP_SRC)/core/memp.c \
	$(LWIP_SRC)/core/netif.c \
	$(LWIP_SRC)/core/pbuf.c \
	$(LWIP_SRC)/core/raw.c \
	$(LWIP_SRC)/core/stats.c \
	$(LWIP_SRC)/core/sys.c \
	$(LWIP_SRC)/core/altcp.c \
	$(LWIP_SRC)/core/altcp_alloc.c \
	$(LWIP_SRC)/core/altcp_tcp.c \
	$(LWIP_SRC)/core/tcp.c \
	$(LWIP_SRC)/core/tcp_in.c \
	$(LWIP_SRC)/core/tcp_out.c \
	$(LWIP_SRC)/core/timeouts.c \
	$(LWIP_SRC)/core/udp.c \
	$(LWIP_SRC)/core/ipv4/acd.c \
	$(LWIP_SRC)/core/ipv4/autoip.c \
	$(LWIP_SRC)/core/ipv4/dhcp.c \
	$(LWIP_SRC)/core/ipv4/etharp.c \
	$(LWIP_SRC)/core/ipv4/icmp.c \
	$(LWIP_SRC)/core/ipv4/igmp.c \
	$(LWIP_SRC)/core/ipv4/ip4.c \
	$(LWIP_SRC)/core/ipv4/ip4_addr.c \
	$(LWIP_SRC)/core/ipv4/ip4_frag.c \
	$(LWIP_SRC)/api/api_lib.c \
	$(LWIP_SRC)/api/api_msg.c \
	$(LWIP_SRC)/api/err.c \
	$(LWIP_SRC)/api/if_api.c \
	$(LWIP_SRC)/api/netbuf.c \
	$(LWIP_SRC)/api/netdb.c \
	$(LWIP_SRC)/api/netifapi.c \
	$(LWIP_SRC)/api/sockets.c \
	$(LWIP_SRC)/api/tcpip.c \
	$(LWIP_SRC)/netif/ethernet.c

LWIP_OBJS := $(patsubst $(LWIP_SRC)/%.c,$(BUILD_DIR)/lwip_%.o,$(LWIP_CORE_SRCS))

PORT_OBJS := \
	$(BUILD_DIR)/sys_arch.o \
	$(BUILD_DIR)/mt7688_eth.o

BSP_OBJS := \
	$(BUILD_DIR)/startup.o \
	$(BUILD_DIR)/mt7688_uart.o \
	$(BUILD_DIR)/mt7688_init.o \
	$(BUILD_DIR)/syscalls.o

APP_SRCS := \
	main.c \
	httpd.c \
	ipp_server.c \
	mdns.c \
	lpr.c

GENERATED_SRCS := $(BUILD_DIR)/web_resources.c

APP_OBJS := $(addprefix $(BUILD_DIR)/, $(APP_SRCS:.c=.o)) \
		$(BUILD_DIR)/web_resources.o

ALL_OBJS := $(BSP_OBJS) $(FRT_OBJS) $(PORT_OBJS) $(LWIP_OBJS) $(APP_OBJS)

.PHONY: all
all: firmware

.PHONY: app-build
app-build: $(ELF_OUT)

$(ELF_OUT): $(ALL_OBJS) | $(BUILD_DIR)
	$(LD) $(ALL_OBJS) $(LDFLAGS) -o $@
	@echo "  Linked: $@"

$(BIN_OUT): $(ELF_OUT)
	$(OBJCOPY) -O binary $< $@
	@echo "  Binary: $@"

.PHONY: firmware
firmware: $(FW_OUT)

$(FW_OUT): $(BIN_OUT)
	python3 $(PKG_SCRIPT) $(BIN_OUT) $(FW_OUT)
	@echo "  Firmware: $(FW_OUT)"

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/startup.o: $(BSP_DIR)/startup.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/port_asm.o: $(FRT_PORT)/port_asm.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/mt7688_uart.o: $(BSP_DIR)/mt7688_uart.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mt7688_init.o: $(BSP_DIR)/mt7688_init.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscalls.o: $(BSP_DIR)/syscalls.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/port.o: $(FRT_PORT)/port.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tasks.o: $(FRT_SRC)/tasks.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/list.o: $(FRT_SRC)/list.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/queue.o: $(FRT_SRC)/queue.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timers.o: $(FRT_SRC)/timers.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/event_groups.o: $(FRT_SRC)/event_groups.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/stream_buffer.o: $(FRT_SRC)/stream_buffer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap_4.o: $(FRT_SRC)/portable/MemMang/heap_4.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sys_arch.o: $(LWIP_PORT)/arch/sys_arch.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mt7688_eth.o: $(NETIF_DIR)/mt7688_eth.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lwip_%.o: $(LWIP_SRC)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(GENERATED_SRCS): $(wildcard $(WEB_DIR)/*) | $(BUILD_DIR)
	python3 $(SRC_DIR)/gen_web_resources.py $(WEB_DIR) $@

$(BUILD_DIR)/web_resources.o: $(GENERATED_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean done."
