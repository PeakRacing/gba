ENTRY(gba_smoke_entry)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 1M
}
SECTIONS {
    .text : { *(.text*) *(.rodata*) } > FLASH
    .data : { *(.data*) } > RAM AT> FLASH
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
    /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.extab*) }
}
