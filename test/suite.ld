ENTRY(_start)
MEMORY {
    rom (rx) : ORIGIN = 0x08000000, LENGTH = 32M
    ewram (rw) : ORIGIN = 0x02000000, LENGTH = 256K
    iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 28K
}
SECTIONS {
    .text : { KEEP(*(.text.start)) KEEP(*(.rodata.savetype)) *(.text*) *(.rodata*) . = ALIGN(4); } > rom
    .iwram : { __iwram_start = .; *(.iwram*) . = ALIGN(4); __iwram_end = .; } > iwram AT> rom
    __iwram_load = LOADADDR(.iwram);
    /* Match devkitARM: ordinary globals are in IWRAM; only explicitly
     * attributed EWRAM data uses the external, slower memory bus. */
    .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > iwram AT> rom
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > iwram
    .ewram : { __ewram_start = .; *(.ewram*) . = ALIGN(4); __ewram_end = .; } > ewram AT> rom
    __ewram_load = LOADADDR(.ewram);
    end = .;
    /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.extab*) }
}
