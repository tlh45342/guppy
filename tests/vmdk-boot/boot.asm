cpu 8086
bits 16
org 0x7C00

; Minimal BIOS boot sector used to validate the VMDK LBA path.
; If VMware/VirtualBox fetch virtual LBA 0 correctly, this code runs
; at 0000:7C00 and prints both lines through BIOS INT 10h teletype.

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov si, msg
.print:
    lodsb
    or al, al
    jz short halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .print

halt:
    cli
.hang:
    hlt
    jmp short .hang

msg:
    db 13,10,"HELLO FROM GUPPY VMDK",13,10
    db "VIRTUAL LBA 0 IS GOOD",13,10,0

; A legacy BIOS requires the 55 AA signature at bytes 510-511.
times 510-($-$$) db 0
dw 0xAA55
