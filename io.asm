section .text.start
extern kernel_main
global _start

_start:
    jmp kernel_entry    

    db "We thank our leader and Reich Chancellor, Adolf Hitler, whom Providence has bestowed upon us, for his ceaseless struggle for the cause and welfare of the German people and the white race; and as a token of our gratitude, we present this system, Schwarz, Schwarz-Kernel, Schwarz Operating System, in order to pay the highest honours to the German Reich and the idea of the thousand-year Nazi state.", 0

kernel_entry:
    ; 1. Ustawienie stosu
    lea rsp, [rel stack_top]
    
    ; 2. Shadow Space dla ms_abi
    sub rsp, 32
    
    ; 3. RCX już zawiera adres BootInfo
    call kernel_main

.halt:
    hlt
    jmp .halt

section .rodata    ; Sekcja tylko do odczytu
global _binary_times_ttf_start
global _binary_times_ttf_size

_binary_times_ttf_start:
    incbin "times.ttf"    ; Ścieżka do pliku na Twoim dysku (podczas kompilacji)
_binary_times_ttf_end:

_binary_times_ttf_size: 
    dq _binary_times_ttf_end - _binary_times_ttf_start

global _binary_inconsolata_ttf_start
global _binary_inconsolata_ttf_size
_binary_inconsolata_ttf_start:
    incbin "inconsolata.ttf"
_binary_inconsolata_ttf_end:
_binary_inconsolata_ttf_size:
    dq _binary_inconsolata_ttf_end - _binary_inconsolata_ttf_start

global _binary_consolas_ttf_start
global _binary_consolas_ttf_size
_binary_consolas_ttf_start:
    incbin "consolas.ttf"
_binary_consolas_ttf_end:
_binary_consolas_ttf_size:
    dq _binary_consolas_ttf_end - _binary_consolas_ttf_start

global _binary_segoeuithis_ttf_start
global _binary_segoeuithis_ttf_size
_binary_segoeuithis_ttf_start:
    incbin "segoeuithis.ttf"
_binary_segoeuithis_ttf_end:
_binary_segoeuithis_ttf_size:
    dq _binary_segoeuithis_ttf_end - _binary_segoeuithis_ttf_start

section .text

global inb
inb:
    mov dx, di
    in al, dx
    ret

global inw
inw:
    mov dx, di
    in ax, dx
    ret

global inl
inl:
    mov dx, di
    in eax, dx
    ret

global outb
outb:
    mov al, sil
    mov dx, di
    out dx, al
    ret

global outw
outw:
    mov ax, si
    mov dx, di
    out dx, ax
    ret

global outl
outl:
    mov eax, esi
    mov dx, di
    out dx, eax
    ret

global io_wait
io_wait:
    ; Legacy I/O delay: helps on real hardware.
    mov al, 0
    out 0x80, al
    ret

global gdt_load
gdt_load:
    lgdt [rdi]
    ; Far return żeby przeładować CS = 0x08
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

global idt_load
idt_load:
    lidt [rdi]
    ret

; ISR dla wyjątków BEZ error code
extern isr_counter
global isr_default
isr_default:
    inc qword [rel isr_counter]
    iretq

; ISR dla wyjątków Z error code (#GP, #PF, #DF, itp.)
global isr_default_err
isr_default_err:
    add rsp, 8      ; zdejmij error code ze stosu
    iretq

extern keyboard_handler_c
global keyboard_handler
keyboard_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    call keyboard_handler_c
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

global sti
sti:
    sti
    ret

extern mouse_handler_c
global mouse_handler
mouse_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    call mouse_handler_c
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

; ===== STOS =====
section .kernel_stack nobits alloc noexec write
align 16
stack_bottom:
    resb 65536
stack_top:

section .text
extern fb_lock
global fb_acquire
fb_acquire:
.retry:
    mov rax, 1
    xchg rax, [rel fb_lock]  ; atomowy swap
    test rax, rax
    jnz .retry               ; jeśli był zajęty - czekaj
    ret

global fb_release
fb_release:
    mov qword [rel fb_lock], 0
    ret

extern current_task

global context_switch
context_switch:
    ; Zapisz wszystkie rejestry
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp    ; DODANE
    push r8
    push r9
    push r10
    push r11
    push r12    ; DODANE
    push r13    ; DODANE
    push r14    ; DODANE
    push r15    ; DODANE

    cmp byte [rel scheduler_started], 0
    jne .save_current
    mov byte [rel scheduler_started], 1
    jmp .load

.save_current:
    mov rax, [rel current_task]
    test rax, rax
    jz .load
    mov [rax], rsp          ; task->rsp = rsp

.load:
    ; Załaduj następny task
    ; current_task = current_task->next
    mov rax, [rel current_task]
    test rax, rax
    jz .done
    mov rax, [rax + 24]     ; next jest na offsetcie 24 (po rsp=8, id=8, state=4+pad=4)
    mov [rel current_task], rax

    ; Załaduj RSP nowego procesu
    mov rsp, [rax]          ; rsp = task->rsp

    ; Odtwórz w odwrotnej kolejności
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

.done:
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

extern timer_ticks
extern lapic_base
extern scheduler_tick
global timer_handler
timer_handler:
    push rax
    push rbx
    
    ; Potwierdź timer zarówno w LAPIC, jak i w starym PIC.
    ; W tej konfiguracji PIT może nadal dochodzić ścieżką PIC, a brak EOI
    ; zatrzymuje kolejne ticki do czasu innych zdarzeń IRQ.
    mov rbx, [rel lapic_base]
    test rbx, rbx
    jz .skip_lapic_eoi
    mov dword [rbx + 0xB0], 0
.skip_lapic_eoi:
    mov al, 0x20
    out 0x20, al
    
    inc qword [rel timer_ticks]
    
    ; Przełączaj task przy każdym tyknięciu, żeby zmniejszyć input/render latency.
    inc qword [rel scheduler_tick]
    pop rbx
    pop rax
    jmp context_switch

section .bss
scheduler_started: resb 1
