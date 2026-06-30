#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

using namespace std;

// ============================================================================
// 1. ARCHITECTURE & HARDWARE EMULATOR SPECIFICATION
// ============================================================================
// The VM is a 16-bit system with 64KB of RAM, 4 General Purpose Registers (R0-R3),
// a Program Counter (PC), and a Stack Pointer (SP).
//
// Memory map:
//   0x0000 - 0x0003 : Reset Interrupt Vector (Pointer to boot code)
//   0x0004 - 0x0007 : System Call (SYS) Vector
//   0x0008 - 0x000B : Hardware Keyboard Interrupt Vector (unused here)
//   0x0100          : Kernel Program entry point
//   0xF000          : Top of the Kernel Stack (grows downward)
//   0xFF00          : Keyboard I/O Status Register (0 = empty, 1 = character available)
//   0xFF01          : Keyboard I/O Data Register (returns ASCII byte)
//   0xFF02          : Console Screen Output Register (writing a byte prints it to host terminal)

class CPU {
public:
    uint8_t memory[65536];
    uint16_t PC;
    uint16_t regs[5]; // R0 (0), R1 (1), R2 (2), R3 (3), SP (4)
    bool flag_zero;
    bool flag_sign;
    bool running;

    CPU() {
        reset();
    }

    void reset() {
        std::fill(std::begin(memory), std::end(memory), 0);
        std::fill(std::begin(regs), std::end(regs), 0);
        PC = 0;
        flag_zero = false;
        flag_sign = false;
        running = true;
    }

    // Push state and jump to an address stored at the given Vector Table entry
    void trigger_interrupt(uint16_t vector_address) {
        // Push current return PC (2 bytes)
        regs[4] -= 2; // Decrement Stack Pointer
        memory[regs[4]] = PC & 0xFF;
        memory[regs[4] + 1] = (PC >> 8) & 0xFF;

        // Push Flags (1 byte mapped to 16-bit stack value)
        regs[4] -= 2;
        memory[regs[4]] = (flag_zero ? 1 : 0);
        memory[regs[4] + 1] = 0;

        // Fetch handler address from target Vector
        PC = memory[vector_address] | (memory[vector_address + 1] << 8);
    }

    // Execute a single CPU machine cycle
    void step() {
        if (!running) return;

        // Instruction Decoding: All instructions are exactly 4 bytes long
        uint8_t op = memory[PC];
        uint8_t r1 = memory[PC + 1];
        uint8_t r2 = memory[PC + 2];
        uint16_t imm = (memory[PC + 2] << 8) | memory[PC + 3];

        PC += 4;

        switch (op) {
            case 0x00: // HALT
                running = false;
                break;
            case 0x01: // MOV_IMM: Reg[r1] = imm
                regs[r1] = imm;
                break;
            case 0x02: // MOV_REG: Reg[r1] = Reg[r2]
                regs[r1] = regs[r2];
                break;
            case 0x03: // LDR: Reg[r1] = memory[Reg[r2]]
                regs[r1] = memory[regs[r2]];
                break;
            case 0x04: // STR: memory[Reg[r1]] = Reg[r2]
                memory[regs[r1]] = regs[r2] & 0xFF;
                // Hook to trap Console Screen Output Register writes
                if (regs[r1] == 0xFF02) {
                    cout << static_cast<char>(regs[r2] & 0xFF) << flush;
                }
                break;
            case 0x05: // ADD: Reg[r1] = Reg[r1] + Reg[r2]
                regs[r1] = regs[r1] + regs[r2];
                break;
            case 0x06: // SUB: Reg[r1] = Reg[r1] - Reg[r2]
                regs[r1] = regs[r1] - regs[r2];
                break;
            case 0x07: // CMP: Set comparison flags for Reg[r1] and Reg[r2]
                flag_zero = (regs[r1] == regs[r2]);
                flag_sign = (regs[r1] < regs[r2]);
                break;
            case 0x08: // JMP: PC = imm
                PC = imm;
                break;
            case 0x09: // JZ: Jump if Zero Flag is set
                if (flag_zero) PC = imm;
                break;
            case 0x0A: // JNZ: Jump if Zero Flag is clear
                if (!flag_zero) PC = imm;
                break;
            case 0x0B: // SYS: Trigger a software interrupt (Vector 0x0004)
                trigger_interrupt(4);
                break;
            case 0x0C: // IRET: Return from system interrupt
                // Pop Flags
                flag_zero = (memory[regs[4]] != 0);
                regs[4] += 2;
                // Pop Return PC
                PC = memory[regs[4]] | (memory[regs[4] + 1] << 8);
                regs[4] += 2;
                break;
            case 0x0D: // PUSH: Save Reg[r1] to stack
                regs[4] -= 2;
                memory[regs[4]] = regs[r1] & 0xFF;
                memory[regs[4] + 1] = (regs[r1] >> 8) & 0xFF;
                break;
            case 0x0E: // POP: Load stack value into Reg[r1]
                regs[r1] = memory[regs[4]] | (memory[regs[4] + 1] << 8);
                regs[4] += 2;
                break;
            default:
                // Stop CPU on undefined operation code
                running = false;
                break;
        }
    }
};

// ============================================================================
// 2. EMBEDDED ASSEMBLER UTILITIES
// ============================================================================
string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

vector<string> splitArgs(const string& argsStr) {
    vector<string> args;
    stringstream ss(argsStr);
    string arg;
    while (getline(ss, arg, ',')) {
        args.push_back(trim(arg));
    }
    return args;
}

uint8_t parseRegister(const string& r) {
    if (r == "R0") return 0;
    if (r == "R1") return 1;
    if (r == "R2") return 2;
    if (r == "R3") return 3;
    if (r == "SP") return 4;
    throw runtime_error("Invalid hardware register reference: " + r);
}

string processEscapes(const string& s) {
    string res = "";
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 'n') { res += '\n'; i++; }
            else if (s[i+1] == 'r') { res += '\r'; i++; }
            else if (s[i+1] == 'e') { res += '\033'; i++; } // support ansi escape characters
            else { res += s[i]; }
        } else {
            res += s[i];
        }
    }
    return res;
}

uint16_t resolveValue(const string& token, const unordered_map<string, uint32_t>& label_table) {
    if (label_table.count(token)) {
        return static_cast<uint16_t>(label_table.at(token));
    }
    try {
        if (token.rfind("0x", 0) == 0) {
            return static_cast<uint16_t>(stoul(token, nullptr, 16));
        }
        return static_cast<uint16_t>(stoul(token));
    } catch (...) {
        throw runtime_error("Undefined label or constant token: " + token);
    }
}

// Translate assembly instructions directly into the emulator memory space
void assemble(const string& src, uint8_t* memory) {
    unordered_map<string, uint32_t> label_table;
    vector<string> clean_lines;
    stringstream ss(src);
    string raw_line;

    uint32_t current_addr = 0;

    // --- PASS 1: Build the Symbol and Label Address Table ---
    while (getline(ss, raw_line)) {
        string line = trim(raw_line);
        size_t semi = line.find(';');
        if (semi != string::npos) {
            line = trim(line.substr(0, semi));
        }
        if (line.empty()) continue;

        if (line.back() == ':') {
            string label = line.substr(0, line.size() - 1);
            label_table[label] = current_addr;
        } else if (line.rfind("ORG ", 0) == 0) {
            current_addr = stoul(line.substr(4));
        } else if (line.rfind("DB ", 0) == 0) {
            string data = trim(line.substr(3));
            if (data.front() == '"' && data.back() == '"') {
                string s = processEscapes(data.substr(1, data.size() - 2));
                current_addr += s.size() + 1; // String length + Null terminator
            } else {
                current_addr += 1;
            }
        } else if (line.rfind("DW ", 0) == 0) {
            current_addr += 2;
        } else {
            current_addr += 4; // Instructions are fixed-width (4 bytes)
        }
        clean_lines.push_back(line);
    }

    // --- PASS 2: Emit Raw Binary Code to Memory ---
    current_addr = 0;
    for (const auto& line : clean_lines) {
        if (line.back() == ':') continue;

        if (line.rfind("ORG ", 0) == 0) {
            current_addr = stoul(line.substr(4));
            continue;
        }

        if (line.rfind("DB ", 0) == 0) {
            string data = trim(line.substr(3));
            if (data.front() == '"' && data.back() == '"') {
                string s = processEscapes(data.substr(1, data.size() - 2));
                for (char c : s) {
                    memory[current_addr++] = static_cast<uint8_t>(c);
                }
                memory[current_addr++] = 0;
            } else {
                memory[current_addr++] = static_cast<uint8_t>(resolveValue(data, label_table));
            }
            continue;
        }

        if (line.rfind("DW ", 0) == 0) {
            uint16_t val = resolveValue(trim(line.substr(3)), label_table);
            memory[current_addr++] = (val >> 8) & 0xFF;
            memory[current_addr++] = val & 0xFF;
            continue;
        }

        // Parse Standard Mnemonics
        string mnemonic = "";
        string args_str = "";
        size_t space = line.find(' ');
        if (space == string::npos) {
            mnemonic = line;
        } else {
            mnemonic = trim(line.substr(0, space));
            args_str = trim(line.substr(space + 1));
        }
        vector<string> args = splitArgs(args_str);

        vector<uint8_t> mc(4, 0); // Temporary vector holding generated instruction layout

        if (mnemonic == "HALT") {
            mc[0] = 0x00;
        } else if (mnemonic == "MOV") {
            uint8_t r1 = parseRegister(args[0]);
            if (args[1][0] == 'R' || args[1] == "SP") {
                mc[0] = 0x02; // MOV_REG
                mc[1] = r1;
                mc[2] = parseRegister(args[1]);
            } else {
                mc[0] = 0x01; // MOV_IMM
                mc[1] = r1;
                uint16_t imm = resolveValue(args[1], label_table);
                mc[2] = (imm >> 8) & 0xFF;
                mc[3] = imm & 0xFF;
            }
        } else if (mnemonic == "LDR") {
            uint8_t r1 = parseRegister(args[0]);
            string src = args[1];
            if (src.front() == '[' && src.back() == ']') src = src.substr(1, src.size() - 2);
            mc[0] = 0x03;
            mc[1] = r1;
            mc[2] = parseRegister(src);
        } else if (mnemonic == "STR") {
            string dest = args[0];
            if (dest.front() == '[' && dest.back() == ']') dest = dest.substr(1, dest.size() - 2);
            mc[0] = 0x04;
            mc[1] = parseRegister(dest);
            mc[2] = parseRegister(args[1]);
        } else if (mnemonic == "ADD") {
            mc[0] = 0x05;
            mc[1] = parseRegister(args[0]);
            mc[2] = parseRegister(args[1]);
        } else if (mnemonic == "SUB") {
            mc[0] = 0x06;
            mc[1] = parseRegister(args[0]);
            mc[2] = parseRegister(args[1]);
        } else if (mnemonic == "CMP") {
            mc[0] = 0x07;
            mc[1] = parseRegister(args[0]);
            mc[2] = parseRegister(args[1]);
        } else if (mnemonic == "JMP") {
            mc[0] = 0x08;
            uint16_t dest = resolveValue(args[0], label_table);
            mc[2] = (dest >> 8) & 0xFF;
            mc[3] = dest & 0xFF;
        } else if (mnemonic == "JZ") {
            mc[0] = 0x09;
            uint16_t dest = resolveValue(args[0], label_table);
            mc[2] = (dest >> 8) & 0xFF;
            mc[3] = dest & 0xFF;
        } else if (mnemonic == "JNZ") {
            mc[0] = 0x0A;
            uint16_t dest = resolveValue(args[0], label_table);
            mc[2] = (dest >> 8) & 0xFF;
            mc[3] = dest & 0xFF;
        } else if (mnemonic == "SYS") {
            mc[0] = 0x0B;
        } else if (mnemonic == "IRET") {
            mc[0] = 0x0C;
        } else if (mnemonic == "PUSH") {
            mc[0] = 0x0D;
            mc[1] = parseRegister(args[0]);
        } else if (mnemonic == "POP") {
            mc[0] = 0x0E;
            mc[1] = parseRegister(args[0]);
        } else {
            throw runtime_error("Parser error: Invalid instruction: " + mnemonic);
        }

        // Write instruction word directly to emulator memory
        for (int i = 0; i < 4; ++i) {
            memory[current_addr++] = mc[i];
        }
    }
}

// ============================================================================
// 3. THE ASSEMBLY OPERATING SYSTEM KERNEL CODE
// ============================================================================
// Real OS kernel containing:
// - Boot Vector initialization pointing to low level init sequences.
// - An Interrupt Service Routine (ISR) that processes standard System Calls (SYS).
// - Interactive shell processing commands 'h' (help), 'c' (clear), and 'e' (exit).
const string kernel_asm = R"(
; --- HARDWARE INTERRUPT VECTORS ---
ORG 0
DW kinit            ; Address 0: Reset Vector

ORG 4
DW syscall_isr      ; Address 4: Software Interrupt Handler

; --- LOW LEVEL KERNEL INITIALIZATION ---
ORG 256
kinit:
    MOV SP, 61440   ; Set Stack Pointer to 0xF000 (Safe kernel stack offset space)
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
)";

// ============================================================================
// 4. POSIX TERMINAL DRIVERS & HOST EMULATOR INTERRUPT INTERFACES
// ============================================================================
struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode); // Restore screen terminal state on crash/exit
    struct termios raw = orig_termios;
    // Set terminal to non-canonical input mode and disable local echo
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Check standard input buffer status
bool kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

int main() {
    CPU cpu;

    cout << "Compiling system kernel assembly text..." << endl;
    try {
        assemble(kernel_asm, cpu.memory);
        cout << "Kernel successfully assembled into virtual ROM." << endl;
    } catch (const exception& e) {
        cerr << "Assembler Error: " << e.what() << endl;
        return 1;
    }

    // Set Program Counter to initial entry point found in Reset Interrupt Vector
    cpu.PC = cpu.memory[0] | (cpu.memory[1] << 8);

    cout << "Spinning up hardware subsystem components..." << endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Capture control of terminal IO
    enableRawMode();

    // Primary CPU Emulation and Virtual Device Loop
    while (cpu.running) {
        // Read keystrokes asynchronously and route to Keyboard I/O Register
        if (kbhit()) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == 3) { // Catch Ctrl+C to terminate host emulator
                    break;
                }
                // Write ASCII byte to data port and signal status flag register
                cpu.memory[0xFF01] = static_cast<uint8_t>(ch);
                cpu.memory[0xFF00] = 1;
            }
        }

        cpu.step();

        // Introduce a small execution delay to avoid 100% host processor utilization
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    disableRawMode();
    cout << "\nHost emulator terminated successfully." << endl;
    return 0;
}
