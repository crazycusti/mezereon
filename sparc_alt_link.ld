ENTRY(_start)
SECTIONS
{
  . = 0x4000;
  .text : { 
    *(.text*) 
  } = 0x01000000  /* Fill with NOPs */
  .rodata ALIGN(8) : { 
    *(.rodata*) 
  }
  .data ALIGN(8) : { 
    *(.data*) 
  }
  .bss ALIGN(8) : { 
    *(.bss*) 
    *(COMMON) 
  }
  
  /* Add some padding */
  . = ALIGN(0x1000);
}
