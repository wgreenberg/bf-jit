BITS 64

; Example nasm for brainfuck ops. To view generated machine code for an op,
; uncomment it and and run:
;     $ nasm operations.S && xxd operations

; >
;add byte rdx, 2

; <
;sub byte rdx, 2

; +
;add byte [rdx], 1

; -
;sub byte [rdx], 1

; ,
;mov rax, 0
;mov rdi, 0
;mov rsi, rdx
;push rdx
;mov rdx, 1
;syscall
;pop rdx

; .
;mov rsi, rdx
;mov rax, 1
;mov rdi, 1
;push rdx
;mov rdx, 1
;syscall
;pop rdx

; initialize rdx
;mov rdx, 0xdeadbeefdeadbee

; exit
;mov rax, 60
;syscall

; jump back to [ if current cell != 0
;cmp byte [rdx], 0
;jne -0x27

; jump forward to ]
;jmp 0x27

; [-]
;mov byte [rdx], 0
