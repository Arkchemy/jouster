/* Minimal NRO-compatible linker script -- no libnx/switch.ld dependency.
 *
 * elf2nro (switch-tools) requires exactly 3 PT_LOAD program headers
 * (code/rodata/data, in that order) followed by a 4th program header used
 * only to compute .bss size from (p_memsz - aligned p_filesz). See
 * elf2nro.c's segment-parsing loop for the exact rule this mirrors.
 *
 * Each section is forced to be at least 1 byte so lld doesn't optimize away
 * an empty PT_LOAD segment and break that 4-phdr shape.
 */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    code   PT_LOAD FLAGS(5); /* R+X */
    rodata PT_LOAD FLAGS(4); /* R   */
    data   PT_LOAD FLAGS(6); /* R+W */
    bss    PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    . = 0x0;
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } :code

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
        BYTE(0)
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data .data.*)
        BYTE(0)
    } :data

    . = ALIGN(0x1000);
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
        . += 1;
    } :bss

    /DISCARD/ : { *(.comment .note* .eh_frame* .ARM.* .gnu.*) }
}
