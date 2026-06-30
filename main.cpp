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
