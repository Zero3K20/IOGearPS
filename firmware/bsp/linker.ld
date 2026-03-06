/*
 * linker.ld — MT7688 linker script for the FreeRTOS firmware.
 *
 * Memory map
 * ──────────
 *   Physical SDRAM:  0x00000000 – 0x03FFFFFF  (64 MB)
 *   KSEG0 alias:     0x80000000 – 0x83FFFFFF  (cached, unmapped)
 *
 *   Exception vector page (runtime-written):  0x80000000 – 0x804FFFFF
 *   Firmware image (loaded by bootloader):    0x80500000 – ...
 *
 * The firmware binary is LZMA-compressed by package_firmware.py.  The ZOT
 * bootloader decompresses it to 0x80500000 (taken from the uImage load_addr
 * field) and branches to the entry point _start at that address.
 * This matches the OEM eCos firmware load address exactly.
 */

OUTPUT_FORMAT("elf32-tradlittlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
    /*
     * Main application region.
     * Starts at 0x80500000 — the ZOT bootloader decompresses the firmware
     * LZMA payload to this address (matching the OEM firmware's uImage
     * load_addr field).  The preceding 5 MB (0x80000000–0x804FFFFF) is
     * used for MIPS exception vectors written at runtime and the FreeRTOS
     * heap extension.
     * Length: 59 MB — leaves 5 MB below for exception vectors / heap.
     */
    ram : ORIGIN = 0x80500000, LENGTH = 59M
}

/* Initial stack for startup / IRQ (before FreeRTOS per-task stacks). */
_stack_size = 0x4000;   /* 16 KB */

SECTIONS
{
    /*
     * .text.startup must be the very first section so that _start lands at
     * 0x80500000 (the bootloader's entry point, matching OEM firmware).
     */
    .startup 0x80500000 :
    {
        KEEP(*(.text.startup))
    } > ram

    /*
     * The firmware version string is placed in a fixed-offset section so
     * that package_firmware.py can embed it in the ZOT header at offset 0x28.
     * This section just needs to be in the binary without clashing.
     */
    .version :
    {
        KEEP(*(.version))
    } > ram

    .text :
    {
        *(.text .text.* .gnu.linkonce.t.*)
    } > ram

    .rodata :
    {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    } > ram

    /* Exception-table / GCC metadata */
    .eh_frame_hdr : { *(.eh_frame_hdr) }  > ram
    .eh_frame     : { KEEP(*(.eh_frame)) } > ram

    /*
     * Small-data disabled (-G 0 in CFLAGS) — .sdata/.sbss are empty.
     * Kept here so the linker does not warn about unplaced input sections.
     */
    .sdata :
    {
        /* MIPS GP register: midpoint of .sdata ± 32KB addressing window.
         * With -G 0 (no small-data), .sdata is empty; this definition is
         * retained so GP-relative addressing still has a valid base. */
        _gp = ALIGN(16) + 0x7FF0;
        *(.sdata .sdata.* .gnu.linkonce.s.*)
    } > ram

    .data :
    {
        *(.data .data.* .gnu.linkonce.d.*)
    } > ram

    .sbss (NOLOAD) :
    {
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
    } > ram

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ram

    /*
     * Startup stack — allocated in BSS (NOLOAD) after all other data.
     * _stack_top is the first address above the stack region; the startup
     * code initialises SP to _stack_top and grows downward.
     */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += _stack_size;
        . = ALIGN(16);
        _stack_top = .;
    } > ram

    /*
     * _end marks the top of statically-allocated RAM.  _heap_start and
     * _heap_end delimit the region given to newlib's malloc via _sbrk().
     * The remaining ~8 MB is used by FreeRTOS's heap_4.c allocator
     * (configTOTAL_HEAP_SIZE = 8 MB, declared as a static array in tasks.c).
     */
    _end        = .;
    _heap_start = .;
    _heap_end   = ORIGIN(ram) + LENGTH(ram);
}
