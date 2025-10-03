[BITS 32]
section .text

global _start

_start:
    ; VGA-Text-Modus Test
    mov eax, 0xB8000
    mov word [eax], 0x0741     ; 'A' in weiß auf schwarz
    mov word [eax+2], 0x0742   ; 'B'
    mov word [eax+4], 0x0743   ; 'C'
.halt:
    hlt
    jmp .halt
