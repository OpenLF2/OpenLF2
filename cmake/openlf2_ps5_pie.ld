/*
 * OpenLF2 PS5 intermediate PIE layout.
 * Separate executable, immutable, RELRO, and writable regions; leave a page
 * between RELRO and data for the PS5 process parameter records added later.
 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
__image_start = 0;

PHDRS {
    text PT_LOAD FLAGS (0x5);
    ro PT_LOAD FLAGS (0x4);
    relro PT_LOAD FLAGS (0x6);
    data PT_LOAD FLAGS (0x6);
    dynamic PT_DYNAMIC FLAGS (0x6);
}

SECTIONS {
    .text 0 : ALIGN(CONSTANT(MAXPAGESIZE)) {
        QUAD(0xcccccccccccccccc)
        QUAD(0xcccccccccccccccc)
        *(.text .text.*)
        *(.plt .plt.*)
    } : text

    .eh_frame_hdr : ALIGN(CONSTANT(MAXPAGESIZE)) {
        __eh_frame_hdr_start = .;
        KEEP(*(.eh_frame_hdr))
        __eh_frame_hdr_end = .;
    } : ro
    .eh_frame : {
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        __eh_frame_end = .;
    } : ro
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } : ro
    .rodata : { *(.rodata .rodata.*) } : ro

    /* The converter maps the RELRO segment from .got's file offset, so .got must open
     * the page-aligned RELRO region; C++ vtables in .data.rel.ro follow it. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .got : { *(.got) } : relro
    .got.plt : { *(.got.plt) } : relro
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } : relro
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } : relro
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } : relro
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } : relro

    /* The converter consumes and rewrites LLVM's dynamic metadata. */
    .dynsym : { *(.dynsym) } : relro
    .gnu.hash : { *(.gnu.hash) } : relro
    .dynstr : { *(.dynstr) } : relro
    .rela.dyn : { *(.rela.dyn) } : relro
    .rela.plt : { *(.rela.plt) } : relro

    /* Reserve a full PS5 page for process parameters before writable data. */
    . = ALIGN(CONSTANT(MAXPAGESIZE)) + CONSTANT(MAXPAGESIZE);
    .dynamic : { *(.dynamic) } : data : dynamic
    .data : { *(.data .data.*) } : data
    .tdata : { *(.tdata .tdata.*) } : data
    .tbss : { *(.tbss .tbss.*) } : data
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } : data
    end = .;
    _end = .;
    __image_end = .;
}
