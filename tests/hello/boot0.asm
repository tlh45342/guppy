bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov si, message

print_loop:
    lodsb
    test al, al
    jz hang

    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_loop

hang:
    cli
.hang:
    hlt
    jmp .hang

message:
    db "HELLO WORLD", 13, 10, 0