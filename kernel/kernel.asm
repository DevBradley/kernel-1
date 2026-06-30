; --- HARDWARE INTERRUPT VECTORS ---
ORG 0
DW kinit            ; Address 0: Reset Vector

ORG 4
DW syscall_isr      ; Address 4: Software Interrupt Handler

; --- LOW LEVEL KERNEL INITIALIZATION ---
ORG 256
kinit:
    MOV SP, 61440   ; Set Stack Pointer to 0xF000
    MOV R1, welcome_msg
    MOV R0, 2       ; Syscall 2: Print Null-Terminated String pointed by R1
    SYS
    JMP shell_prompt

shell_prompt:
    MOV R1, prompt_msg
    MOV R0, 2
    SYS

    ; Reset command buffer state variables
    MOV R1, buf_index
    MOV R2, 0
    STR [R1], R2    ; Clear index tracker: buf_index = 0

shell_wait:
    ; Hardware Device I/O Polling Loop: Wait for serial keyboard character
    MOV R1, 65280   ; Address 0xFF00: Keyboard Status Flag
    LDR R2, [R1]
    MOV R3, 0
    CMP R2, R3
    JZ shell_wait   ; Jump back if status is 0 (no keystroke)

    ; Read keystroke byte
    MOV R1, 65281   ; Address 0xFF01: Keyboard Data Register
    LDR R2, [R1]    ; Fetch character

    ; Clear the hardware interface keyboard flag
    MOV R1, 65280
    MOV R3, 0
    STR [R1], R3

    ; Echo received key back to output terminal
    MOV R1, R2
    MOV R0, 1       ; Syscall 1: Print Character
    SYS

    ; If keystroke matches Enter (CR '\r' (13) or LF '\n' (10)), execute command interpreter
    MOV R3, 13
    CMP R2, R3
    JZ handle_enter

    MOV R3, 10
    CMP R2, R3
    JZ handle_enter

    ; Else, store keyboard character into local memory buffer
    MOV R1, buf_index
    LDR R3, [R1]    ; Fetch current buffer index
    MOV R1, cmd_buffer
    ADD R1, R3      ; Add base address to index to obtain pointer offset
    STR [R1], R2    ; Store key in buffer

    ; Increment index and check buffer boundaries
    ADD R3, 1
    MOV R1, buf_index
    STR [R1], R3
    JMP shell_wait

handle_enter:
    ; Terminate command buffer string with null byte
    MOV R1, buf_index
    LDR R3, [R1]
    MOV R1, cmd_buffer
    ADD R1, R3
    MOV R2, 0
    STR [R1], R2

    ; Print standard newline to terminal
    MOV R1, newline_msg
    MOV R0, 2
    SYS

    ; Verify if command string has length > 0
    MOV R1, buf_index
    LDR R3, [R1]
    MOV R2, 0
    CMP R3, R2
    JZ shell_prompt ; If empty string, simply redraw prompt

    ; --- KERNEL SHELL COMMAND COMPARATOR INTERPRETER ---
    ; Read first char of command buffer from memory
    MOV R1, cmd_buffer
    LDR R2, [R1]

    ; Check 'h' - Help
    MOV R3, 104
    CMP R2, R3
    JZ cmd_help

    ; Check 'c' - Clear Screen
    MOV R3, 99
    CMP R2, R3
    JZ cmd_clear

    ; Check 'e' - Exit Virtual Environment
    MOV R3, 101
    CMP R2, R3
    JZ cmd_exit

    ; Default: Unregistered Command
    MOV R1, error_msg
    MOV R0, 2
    SYS
    JMP shell_prompt

cmd_help:
    MOV R1, help_msg
    MOV R0, 2
    SYS
    JMP shell_prompt

cmd_clear:
    MOV R1, clear_msg
    MOV R0, 2
    SYS
    JMP shell_prompt

cmd_exit:
    MOV R1, exit_msg
    MOV R0, 2
    SYS
    HALT            ; CPU shutdown sequence

; --- SYSTEM CALL HANDLER (INTERRUPT DRIVEN) ---
syscall_isr:
    MOV R3, 1
    CMP R0, R3
    JZ sys_print_char

    MOV R3, 2
    CMP R0, R3
    JZ sys_print_string

    IRET            ; Return on unrecognized syscall ID

sys_print_char:
    ; Print single character stored in R1
    MOV R3, 65282   ; Address 0xFF02: Virtual Screen Output Port
    STR [R3], R1
    IRET

sys_print_string:
    ; Print dynamic null-terminated string starting at address inside R1
sys_print_str_loop:
    LDR R2, [R1]    ; Load current character byte
    MOV R3, 0
    CMP R2, R3
    JZ sys_print_str_done ; If character is 0 (null terminator), exit routine
    
    MOV R3, 65282
    STR [R3], R2    ; Send character byte to serial monitor
    
    MOV R3, 1
    ADD R1, R3      ; Increment string address
    JMP sys_print_str_loop
sys_print_str_done:
    IRET

; --- KERNEL DATA SECTION ---
welcome_msg: DB "\n\r=== MicroOS 16-bit C++ Kernel Booted ===\n\rAvailable commands: 'h' (help), 'c' (clear screen), 'e' (exit)\n\r"
prompt_msg:  DB "micro_os# "
help_msg:    DB "MicroOS Commands:\n\r  h - Displays system commands list\n\r  c - Clear standard console screen\n\r  e - Safely terminates OS environment\n\r"
error_msg:   DB "Command not found. Type 'h' for help.\n\r"
exit_msg:    DB "Initiating kernel shutdown procedure... VM halted.\n\r"
clear_msg:   DB "\e[2J\e[H" ; ANSI sequences for screen clear and cursor home reset
newline_msg: DB "\n\r"

buf_index:   DW 0
cmd_buffer:  DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; 16-byte command buffer
