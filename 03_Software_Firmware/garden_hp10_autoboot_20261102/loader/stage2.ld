OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x00C00000, LENGTH = 64K }
SECTIONS {
  . = ORIGIN(ram);
  .init : { KEEP(*(.init)) } >ram
  .text : { *(.text .text.*) } >ram
  .rodata : { *(.rodata .rodata.*) } >ram
  .data : { *(.data .data.*) *(.sdata .sdata.*) } >ram
  . = ALIGN(4); __binary_end = .;
  __global_pointer$ = . + 0x800;
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) . = ALIGN(4); __bss_end = .; } >ram
  __stack_top = ORIGIN(ram) + LENGTH(ram);
  ASSERT(__bss_end + 4096 <= __stack_top, "stage2 requires 4KB stack headroom")
  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) *(.eh_frame*) }
}
