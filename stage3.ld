ENTRY(stage3_entry)

SECTIONS
{
  . = 0;

  .text :
  {
    KEEP(*(.text.stage3_entry))
    KEEP(*(.text.stage3_entry.*))
    *(.text*)
  }

  .rodata :
  {
    *(.rodata*)
  }

  .data :
  {
    *(.data*)
  }

  .bss :
  {
    *(.bss*)
    *(COMMON)
  }
}
