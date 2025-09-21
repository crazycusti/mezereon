OUTPUT_ARCH(sparc)
OUTPUT_FORMAT("elf32-sparc","elf32-sparc","elf32-sparc")
ENTRY(_start)

/* EIN PT_LOAD (R|X), das auch FILEHDR+PHDRS abdeckt */
PHDRS {
  text PT_LOAD FLAGS(0x5) FILEHDR PHDRS;   /* 0x5 = PF_R | PF_X */
}

SECTIONS
{
  /* Segmentbasis VMA 0: Header/PHDRS im selben LOAD */
  . = 0;

  /* Echte Datei-Bytes als Padding (aus pad.o) – im selben LOAD */
  .pad : {
    KEEP(*(.pad.fill))
  } :text

  /* Code+rodata – ebenfalls im selben LOAD */
  .text 0x4000 : {
    *(.text .text.*)
    *(.rodata .rodata.*)
  } :text

  /* BSS nur im Speicher */
  . = ALIGN(4096);
  .bss (NOLOAD) : {
    *(.bss .bss.*)
    *(COMMON)
  }
}
