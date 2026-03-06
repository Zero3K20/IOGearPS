/*
 * linker.ld — MT7688 linker script for the FreeRTOS firmware.
 *
 * Memory map
 * ──────────
 *   Physical SDRAM:  0x00000000 – 0x03FFFFFF  (64 MB)
 *   KSEG0 alias:     0x80000000 – 0x83FFFFFF  (cached, unmapped)
 *
 *   Exception vector page (runtime-written):  0x80000000 – 0x800003FF
 *   Firmware image (loaded by bootloader):    0x80000400 – ...
 *
 * The firmware binary is LZMA-compressed by package_firmware.py and
 * decompressed by the bootloader to 0x80000400; the entry point _start
 * lives in the .text.startup section which the linker places first.
 */

OUTPUT_FORMAT("elf32-tradlittlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY
{
    /*
     * Main application region.
     * Starts at 0x80000400 (first 1 KiB reserved for MIPS exception vectors,
     * which we write at runtime to the DRAM below our load address).
     * Length: 56 MB — leaves 8 MB for FreeRTOS heap (configTOTAL_HEAP_SIZE).
     */
    ram : ORIGIN = 0x80000400, LENGTH = 56M
}

/* Initial stack for startup / IRQ (before FreeRTOS per-task stacks). */
_stack_size = 0x4000;   /* 16 KB */

SECTIONS
{
    /*
     * .text.startup must be the very first section so that _start lands at
     * 0x80000400 (the bootloader's entry point).
     */
    .startup 0x80000400 :
    {
        KEEP(*(.text.startup))
    } > ram

    /*
     * The firmware version string is placed in a fixed-offset section so
     * that tools/unpack_gpsu21.py can locate it at offset 0x28 in the ZOT
     * header.  package_firmware.py embeds it there separately, so this
     * section just needs to be in the binary without clashing.
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
