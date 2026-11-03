OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { slot (rwx) : ORIGIN = 0x00001000, LENGTH = 124K }
SECTIONS {
  .init : { KEEP(*(.init)) } >slot
  ASSERT(SIZEOF(.init) <= 0x1F000, "bootstrap exceeds immutable ROM copy size")
  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) *(.eh_frame*) }
}
