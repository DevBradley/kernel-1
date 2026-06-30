#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include "cpu.hpp"
#include "assembler.hpp"

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

bool kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

int main() {
    CPU cpu;

    std::cout << "Loading Kernel file: kernel/kernel.asm..." << std::endl;
    std::ifstream file("kernel/kernel.asm");
    if (!file.is_open()) {
        std::cerr << "Error: Could not locate kernel/kernel.asm file" << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string asm_source = buffer.str();

    std::cout << "Assembling Kernel instructions..." << std::endl;
    try {
        assemble(asm_source, cpu.memory);
        std::cout << "Assembler processed code successfully into ROM map." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Assembly Error: " << e.what() << std::endl;
        return 1;
    }

    cpu.PC = cpu.memory[0] | (cpu.memory[1] << 8);

    std::cout << "Starting Emulator instance..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    enableRawMode();

    while (cpu.running) {
        if (kbhit()) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == 3) { // Catch Ctrl+C
                    break;
                }
                cpu.memory[0xFF01] = static_cast<uint8_t>(ch);
                cpu.memory[0xFF00] = 1;
            }
        }

        cpu.step();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    disableRawMode();
    std::cout << "\nEmulator halted execution." << std::endl;
    return 0;
}
