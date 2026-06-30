#pragma once
#include <iostream>
#include <vector>
#include <algorithm>

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

    void trigger_interrupt(uint16_t vector_address) {
        regs[4] -= 2; // Decrement SP
        memory[regs[4]] = PC & 0xFF;
        memory[regs[4] + 1] = (PC >> 8) & 0xFF;

        regs[4] -= 2;
        memory[regs[4]] = (flag_zero ? 1 : 0);
        memory[regs[4] + 1] = 0;

        PC = memory[vector_address] | (memory[vector_address + 1] << 8);
    }

    void step() {
        if (!running) return;

        uint8_t op = memory[PC];
        uint8_t r1 = memory[PC + 1];
        uint8_t r2 = memory[PC + 2];
        uint16_t imm = (memory[PC + 2] << 8) | memory[PC + 3];

        PC += 4;

        switch (op) {
            case 0x00: // HALT
                running = false;
                break;
            case 0x01: // MOV_IMM
                regs[r1] = imm;
                break;
            case 0x02: // MOV_REG
                regs[r1] = regs[r2];
                break;
            case 0x03: // LDR
                regs[r1] = memory[regs[r2]];
                break;
            case 0x04: // STR
                memory[regs[r1]] = regs[r2] & 0xFF;
                if (regs[r1] == 0xFF02) {
                    std::cout << static_cast<char>(regs[r2] & 0xFF) << std::flush;
                }
                break;
            case 0x05: // ADD
                regs[r1] = regs[r1] + regs[r2];
                break;
            case 0x06: // SUB
                regs[r1] = regs[r1] - regs[r2];
                break;
            case 0x07: // CMP
                flag_zero = (regs[r1] == regs[r2]);
                flag_sign = (regs[r1] < regs[r2]);
                break;
            case 0x08: // JMP
                PC = imm;
                break;
            case 0x09: // JZ
                if (flag_zero) PC = imm;
                break;
            case 0x0A: // JNZ
                if (!flag_zero) PC = imm;
                break;
            case 0x0B: // SYS
                trigger_interrupt(4);
                break;
            case 0x0C: // IRET
                flag_zero = (memory[regs[4]] != 0);
                regs[4] += 2;
                PC = memory[regs[4]] | (memory[regs[4] + 1] << 8);
                regs[4] += 2;
                break;
            case 0x0D: // PUSH
                regs[4] -= 2;
                memory[regs[4]] = regs[r1] & 0xFF;
                memory[regs[4] + 1] = (regs[r1] >> 8) & 0xFF;
                break;
            case 0x0E: // POP
                regs[r1] = memory[regs[4]] | (memory[regs[4] + 1] << 8);
                regs[4] += 2;
                break;
            default:
                running = false;
                break;
        }
    }
};
