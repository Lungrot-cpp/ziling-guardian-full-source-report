OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0, LENGTH = 128K }
SECTIONS {
  .text : { KEEP(*(.text.start)) *(.text*) } > RAM
  .rodata : { *(.rodata*) *(.srodata*) } > RAM
  .data : { *(.data*) *(.sdata*) } > RAM
  .bss (NOLOAD) : {
    . = ALIGN(4); _bss_start = .;
    *(.bss*) *(.sbss*) *(COMMON)
    . = ALIGN(4); _bss_end = .;
  } > RAM
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  ASSERT(_bss_end < _stack_top - 8192, "Sensor firmware leaves less than 8 KB stack")
}
