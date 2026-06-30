#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include "cpu.hpp"

inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline std::vector<std::string> splitArgs(const std::string& argsStr) {
    std::vector<std::string> args;
    std::stringstream ss(argsStr);
    std::string arg;
    while (std::getline(ss, arg, ',')) {
        args.push_back(trim(arg));
    }
    return args;
}

inline uint8_t parseRegister(const std::string& r) {
    if (r == "R0") return 0;
    if (r == "R1") return 1;
    if (r == "R2") return 2;
    if (r == "R3") return 3;
    if (r == "SP") return 4;
    throw std::runtime_error("Invalid register reference: " + r);
}

inline std::string processEscapes(const std::string& s) {
    std::string res = "";
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 'n') { res += '\n'; i++; }
            else if (s[i+1] == 'r') { res += '\r'; i++; }
            else if (s[i+1] == 'e') { res += '\033'; i++; }
            else { res += s[i]; }
        } else {
            res += s[i];
        }
    }
    return res;
}

inline uint16_t resolveValue(const std::string& token, const std::unordered_map<std::string, uint32_t>& label_table) {
    if (label_table.count(token)) {
        return static_cast<uint16_t>(label_table.at(token));
    }
    try {
        if (token.rfind("0x", 0) == 0) {
            return static_cast<uint16_t>(std::stoul(token, nullptr, 16));
        }
        return static_cast<uint16_t>(std::stoul(token));
    } catch (...) {
        throw std::runtime_error("Undefined label: " + token);
    }
}

inline void assemble(const std::string& src, uint8_t* memory) {
    std::unordered_map<std::string, uint32_t> label_table;
    std::vector<std::string> clean_lines;
    std::stringstream ss(src);
    std::string raw_line;

    uint32_t current_addr = 0;

    while (std::getline(ss, raw_line)) {
        std::string line = trim(raw_line);
        size_t semi = line.find(';');
        if (semi != std::string::npos) {
            line = trim(line.substr(0, semi));
        }
        if (line.empty()) continue;

        if (line.back() == ':') {
            std::string label = line.substr(0, line.size() - 1);
            label_table[label] = current_addr;
        } else if (line.rfind("ORG ", 0) == 0) {
            current_addr = std::stoul(line.substr(4));
        } else if (line.rfind("DB ", 0) == 0) {
            std::string data = trim(line.substr(3));
            if (data.front() == '"' && data.back() == '"') {
                std::string s = processEscapes(data.substr(1, data.size() - 2));
                current_addr += s.size() + 1;
            } else {
                current_addr += 1;
            }
        } else if (line.rfind("DW ", 0) == 0) {
            current_addr += 2;
        } else {
            current_addr += 4;
        }
        clean_lines.push_back(line);
    }

    current_addr = 0;
    for (const auto& line : clean_lines) {
        if (line.back() == ':') continue;

        if (line.rfind("ORG ", 0) == 0) {
            current_addr = std::stoul(line.substr(4));
            continue;
        }

        if (line.rfind("DB ", 0) == 0) {
            std::string data = trim(line.substr(3));
            if (data.front() == '"' && data.back() == '"') {
                std::string s = processEscapes(data.substr(1, data.size() - 2));
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

        std::string mnemonic = "";
        std::string args_str = "";
        size_t space = line.find(' ');
        if (space == std::string::npos) {
            mnemonic = line;
        } else {
            mnemonic = trim(line.substr(0, space));
            args_str = trim(line.substr(space + 1));
        }
        std::vector<std::string> args = splitArgs(args_str);

        std::vector<uint8_t> mc(4, 0);

        if (mnemonic == "HALT") {
            mc[0] = 0x00;
        } else if (mnemonic == "MOV") {
            uint8_t r1 = parseRegister(args[0]);
            if (args[1][0] == 'R' || args[1] == "SP") {
                mc[0] = 0x02;
                mc[1] = r1;
                mc[2] = parseRegister(args[1]);
            } else {
                mc[0] = 0x01;
                mc[1] = r1;
                uint16_t imm = resolveValue(args[1], label_table);
                mc[2] = (imm >> 8) & 0xFF;
                mc[3] = imm & 0xFF;
            }
        } else if (mnemonic == "LDR") {
            uint8_t r1 = parseRegister(args[0]);
            std::string src = args[1];
            if (src.front() == '[' && src.back() == ']') src = src.substr(1, src.size() - 2);
            mc[0] = 0x03;
            mc[1] = r1;
            mc[2] = parseRegister(src);
        } else if (mnemonic == "STR") {
            std::string dest = args[0];
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
        }

        for (int i = 0; i < 4; ++i) {
            memory[current_addr++] = mc[i];
        }
    }
}
