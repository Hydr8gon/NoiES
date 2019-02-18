#include <chrono>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "GL/glut.h"

uint8_t memory[0x10000];
uint8_t ppu_memory[0x4000];
uint8_t spr_memory[0x100];

uint32_t cycles;
uint16_t program_counter;
uint8_t stack_pointer = 0xFF;
uint8_t accumulator;
uint8_t register_x;
uint8_t register_y;
uint8_t flags = 0x24; // NV_BDIZC
bool interrupts[3];

uint32_t ppu_cycles;
uint32_t scanline_cycles;
uint16_t scanline;
uint8_t ppu_latch;
bool ppu_latch_on;

uint8_t input_shift;

FILE *rom;
uint8_t mapper_type;
uint8_t mapper_registers[2];
uint8_t mapper_shift;
uint16_t rom_address;
uint32_t rom_address_last;
uint32_t vrom_address;

uint32_t framebuffer[256 * 240];
std::chrono::steady_clock::time_point timer;

const char keymap[] = { 'l', 'k', 'g', 'h', 'w', 's', 'a', 'd' };

const uint32_t palette[] = {
    0x757575FF, 0x271B8FFF, 0x0000ABFF, 0x47009FFF,
    0x8F0077FF, 0xAB0013FF, 0xA70000FF, 0x7F0B00FF,
    0x432F00FF, 0x004700FF, 0x005100FF, 0x003F17FF,
    0x1B3F5FFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xBCBCBCFF, 0x0073EFFF, 0x233BEFFF, 0x8300F3FF,
    0xBF00BFFF, 0xE7005BFF, 0xDB2B00FF, 0xCB4F0FFF,
    0x8B7300FF, 0x009700FF, 0x00AB00FF, 0x00933BFF,
    0x00838BFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0x3FBFFFFF, 0x5F97FFFF, 0xA78BFDFF,
    0xF77BFFFF, 0xFF77B7FF, 0xFF7763FF, 0xFF9B3BFF,
    0xF3BF3FFF, 0x83D313FF, 0x4FDF4BFF, 0x58F898FF,
    0x00EBDBFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0xABE7FFFF, 0xC7D7FFFF, 0xD7CBFFFF,
    0xFFC7FFFF, 0xFFC7DBFF, 0xFFBFB3FF, 0xFFDBABFF,
    0xFFE7A3FF, 0xE3FFA3FF, 0xABF3BFFF, 0xB3FFFCFF,
    0x9FFFF3FF, 0x000000FF, 0x000000FF, 0x000000FF
};

// Zero page addressing: Use the immediate value as a memory address
uint8_t *zero_page() {
    return &memory[memory[++program_counter]];
}

// Zero page X addressing: Add the X register to the zero page address
uint8_t *zero_page_x() {
    return &memory[(memory[++program_counter] + register_x) % 0x0100];
}

// Zero page Y addressing: Add the Y register to the zero page address
uint8_t *zero_page_y() {
    return &memory[(memory[++program_counter] + register_y) % 0x0100];
}

// Absolute addressing: Use the immediate 2 values as a memory address
uint8_t *absolute() {
    return &memory[memory[++program_counter] | (memory[++program_counter] << 8)];
}

// Absolute X addressing: Add the X register to the absolute address
uint8_t *absolute_x() {
    return &memory[(memory[++program_counter] | (memory[++program_counter] << 8)) + register_x];
}

// Absolute Y addressing: Add the Y register to the absolute address
uint8_t *absolute_y() {
    return &memory[(memory[++program_counter] | (memory[++program_counter] << 8)) + register_y];
}

// Indirect addressing: Use the memory address stored at the absolute address
uint8_t *indirect() {
    uint8_t *address = absolute();
    return &memory[*address | (*(address + 1) << 8)];
}

// Indirect X addressing: Use the memory address stored at the zero page X address 
uint8_t *indirect_x() {
    uint8_t *address = zero_page_x();
    return &memory[*address | (*(address + 1) << 8)];
}

// Indirect Y addressing: Add the Y register to the memory address stored at the zero page address
uint8_t *indirect_y() {
    uint8_t *address = zero_page();
    return &memory[(*address | (*(address + 1) << 8)) + register_y];
}

// Get a value using immediate addressing
uint8_t *immediate() {
    return &memory[++program_counter];
}

// CL_: Clear a flag
void cl_(uint8_t flag) {
    flags &= ~flag;
}

// SE_: Set a flag
void se_(uint8_t flag) {
    flags |= flag;
}

// PH_: Push a register to the stack
void ph_(uint8_t reg) {
    memory[0x0100 + stack_pointer--] = reg;
}

// PL_: Pull a register from the stack
void pl_(uint8_t *reg) {
    *reg = memory[0x0100 + ++stack_pointer];
}

// ADC: Add with carry
void adc(uint8_t value) {
    uint8_t accum_old = accumulator;
    accumulator += value + (flags & 0x01);

    if (accumulator & 0x80)                                                     se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                       se_(0x02); else cl_(0x02); // Z
    if (value & 0x80 == accum_old & 0x80 && accumulator & 0x80 != value & 0x80) se_(0x40); else cl_(0x40); // V
    if (accum_old > accumulator)                                                se_(0x01); else cl_(0x01); // C
}

// AND: Bitwise and
void _and(uint8_t value) {
    accumulator &= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// ASL: Arithmetic shift left
void asl(uint8_t *value) {
    uint8_t value_old = *value;
    *value <<= 1;

    if (*value & 0x80)    se_(0x80); else cl_(0x80); // N
    if (*value == 0)      se_(0x02); else cl_(0x02); // Z
    if (value_old & 0x80) se_(0x01); else cl_(0x01); // C
}

// BIT: Test bits
void bit(uint8_t value) {
    if (value & 0x80)             se_(0x80); else cl_(0x80); // N
    if (value & 0x40)             se_(0x40); else cl_(0x40); // V
    if (accumulator & value == 0) se_(0x02); else cl_(0x02); // Z
}

// B__: Branch on condition
void b__(bool condition) {
    int8_t value = *immediate();
    if (condition)
        program_counter += value;
}

// BRK: Break
void brk() {
    if (!(flags & 0x04))
        interrupts[2] = true;
    program_counter++;
}

// CP_: Compare a register
void cp_(uint8_t reg, uint8_t value) {
    if ((reg - value) & 0x80) se_(0x80); else cl_(0x80); // N
    if (reg == value)         se_(0x02); else cl_(0x02); // Z
    if (reg >= value)         se_(0x01); else cl_(0x01); // C
}

// DE_: Decrement a value
void de_(uint8_t *value) {
    (*value)--;

    if (*value & 0x80) se_(0x80); else cl_(0x80); // N
    if (*value == 0)   se_(0x02); else cl_(0x02); // Z
}

// EOR: Bitwise exclusive or
void eor(uint8_t value) {
    accumulator ^= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// IN_: Increment a value
void in_(uint8_t *value) {
    (*value)++;

    if (*value & 0x80) se_(0x80); else cl_(0x80); // N
    if (*value == 0)   se_(0x02); else cl_(0x02); // Z
}

// JMP: Jump
void jmp(uint8_t *address) {
    program_counter = address - memory - 1;
}

// JSR: Jump to subroutine
void jsr(uint8_t *address) {
    ph_(program_counter >> 8);
    ph_(program_counter);
    program_counter = address - memory - 1;
}

// LD_: Load a register
void ld_(uint8_t *reg, uint8_t *value) {
    uint16_t address = value - memory;

    // Mirror addresses
    if (address >= 0x0800 && address < 0x2000)
        address = address % 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + (address - 0x2000) % 8;

    *reg = memory[address];

    if (*reg & 0x80) se_(0x80); else cl_(0x80); // N
    if (*reg == 0)   se_(0x02); else cl_(0x02); // Z

    // Handle I/O and PPU registers
    switch (address) {
        case 0x4016: // JOYPAD1: Read button status 1 bit at a time
            *reg = (memory[0x4016] & (1 << input_shift)) ? 1 : 0;
            input_shift++;
            if (input_shift == 8)
                input_shift = 0;
            break;

        case 0x2002: // PPUSTATUS: Clear V-blank bit and latch
            memory[0x2002] &= ~0x80;
            ppu_latch_on = false;
            break;

        case 0x2004: // OAMDATA: Read from sprite memory
            *reg = spr_memory[memory[0x2003]];
            break;

        case 0x2007: // PPUDATA: Read from PPU memory
            uint16_t ppu_address = (ppu_latch << 8) | memory[0x2006];

            // Mirror addresses
            uint16_t address_mirror = ppu_address % 0x4000;
            if (address_mirror >= 0x3000 && address_mirror < 0x3F00)
                address_mirror -= 0x1000;
            else if (address_mirror >= 0x3F20)
                address_mirror = 0x3F00 + (address_mirror - 0x3F20) % 20;

            *reg = ppu_memory[address_mirror];

            // Increment the address
            ppu_address += (memory[0x2000] & 0x04) ? 32 : 1;
            ppu_latch = ppu_address >> 8;
            memory[0x2006] = ppu_address;

            break;
    }
}

// LSR: Logical shift right
void lsr(uint8_t *value) {
    uint8_t value_old = *value;
    *value = (*value >> 1) & ~0x80;

    if (*value & 0x80)    se_(0x80); else cl_(0x80); // N
    if (*value == 0)      se_(0x02); else cl_(0x02); // Z
    if (value_old & 0x01) se_(0x01); else cl_(0x01); // C
}

// ORA: Bitwise or
void ora(uint8_t value) {
    accumulator |= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// T__: Transfer one register to another
void t__(uint8_t *src, uint8_t *dst) {
    *dst = *src;

    if (*dst & 0x80) se_(0x80); else cl_(0x80); // N
    if (*dst == 0)   se_(0x02); else cl_(0x02); // Z
}

// ROL: Rotate left
void rol(uint8_t *value) {
    uint8_t value_old = *value;
    *value = (*value << 1) | (flags & 0x01);

    if (*value & 0x80)    se_(0x80); else cl_(0x80); // N
    if (*value == 0)      se_(0x02); else cl_(0x02); // Z
    if (value_old & 0x80) se_(0x01); else cl_(0x01); // C
}

// ROR: Rotate right
void ror(uint8_t *value) {
    uint8_t value_old = *value;
    *value = ((*value >> 1) & ~0x80) | ((flags & 0x01) << 7);

    if (*value & 0x80)    se_(0x80); else cl_(0x80); // N
    if (*value == 0)      se_(0x02); else cl_(0x02); // Z
    if (value_old & 0x01) se_(0x01); else cl_(0x01); // C
}

// RTI: Return from interrupt
void rti() {
    cl_(0x04);
    pl_(&flags);
    program_counter = memory[0x0100 + ++stack_pointer] | (memory[0x0100 + ++stack_pointer] << 8);
}

// RTS: Return from subroutine
void rts() {
    program_counter = memory[0x0100 + ++stack_pointer] | (memory[0x0100 + ++stack_pointer] << 8);
}

// SBC: Subtract with carry
void sbc(uint8_t value) {
    uint8_t accum_old = accumulator;
    accumulator -= value + !(flags & 0x01);

    if (accumulator & 0x80)                                                     se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                       se_(0x02); else cl_(0x02); // Z
    if (value & 0x80 != accum_old & 0x80 && accumulator & 0x80 == value & 0x80) se_(0x40); else cl_(0x40); // V
    if (accum_old < accumulator)                                                se_(0x01); else cl_(0x01); // C
}

// ST_: Store a register
void st_(uint8_t reg, uint8_t *dst) {
    uint16_t address = dst - memory;

    // Mirror addresses
    if (address >= 0x0800 && address < 0x2000)
        address = address % 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + (address - 0x2000) % 8;

    if (address >= 0x8000) {
        // Handle mapper registers
        switch (mapper_type) {
            case 1: // MMC1: Swap 16 KB or 32 KB ROM banks and 4 KB or 8 KB VROM banks, written to 1 byte at a time
                if (reg & 0x80) {
                    mapper_registers[0] = 0;
                    mapper_shift = 0;
                } else {
                    mapper_registers[0] |= (reg & 0x01) << mapper_shift;
                    mapper_shift++;
                }

                if (mapper_shift == 5) {
                    if (address >= 0x8000 && address < 0xA000) { // Control
                        mapper_registers[1] = mapper_registers[0];
                    }
                    else if (address >= 0xA000 && address < 0xC000) { // VROM bank 0
                        if (mapper_registers[1] & 0x10) { // 4 KB
                            fseek(rom, vrom_address + 0x1000 * mapper_registers[0], SEEK_SET);
                            fread(ppu_memory, 1, 0x1000, rom);
                        }
                        else { // 8 KB
                            fseek(rom, vrom_address + 0x1000 * (mapper_registers[0] & ~0x01), SEEK_SET);
                            fread(ppu_memory, 1, 0x2000, rom);
                        }
                    }
                    else if (address >= 0xC000 && address < 0xE000) { // VROM bank 1
                        if (mapper_registers[1] & 0x10) { // 4 KB
                            fseek(rom, vrom_address + 0x1000 * (mapper_registers[0] + 1), SEEK_SET);
                            fread(&ppu_memory[0x1000], 1, 0x1000, rom);
                        }
                    }
                    else { // ROM banks
                        if (mapper_registers[1] & 0x04) { // ROM bank 1 is fixed
                            if (mapper_registers[1] & 0x08) { // 16 KB
                                fseek(rom, rom_address + 0x4000 * mapper_registers[0], SEEK_SET);
                                fread(&memory[0x8000], 1, 0x4000, rom);
                            }
                            else { // 32 KB
                                fseek(rom, rom_address + 0x4000 * (mapper_registers[0] & 0x0C), SEEK_SET);
                                fread(&memory[0x8000], 1, 0x8000, rom);
                            }

                            fseek(rom, rom_address_last, SEEK_SET);
                            fread(&memory[0xC000], 1, 0x4000, rom);
                        }
                        else { // ROM bank 0 is fixed
                            if (mapper_registers[1] & 0x08) { // 16 KB
                                fseek(rom, rom_address + 0x4000 * mapper_registers[0], SEEK_SET);
                                fread(&memory[0xC000], 1, 0x4000, rom);
                            }
                            else { // 32 KB
                                fseek(rom, rom_address + 0x4000 * (mapper_registers[0] & 0x0C), SEEK_SET);
                                fread(&memory[0x8000], 1, 0x8000, rom);
                            }

                            fseek(rom, rom_address, SEEK_SET);
                            fread(&memory[0x8000], 1, 0x4000, rom);
                        }
                    }

                    mapper_registers[0] = 0;
                    mapper_shift = 0;
                }
                break;

            case 2: // UNROM: Swap the first ROM bank only
                fseek(rom, rom_address + 0x4000 * reg, SEEK_SET);
                fread(&memory[0x8000], 1, 0x4000, rom);
                break;

            case 3: // CNROM: Swap the VROM bank only
                fseek(rom, vrom_address + 0x2000 * (reg & 0x03), SEEK_SET);
                fread(ppu_memory, 1, 0x2000, rom);
                break;
        }
    } else if (address != 0x4016) {
        memory[address] = reg;
    }

    // Handle PPU registers
    switch (address) {
        case 0x4014: // OAMDMA: DMA transfer to sprite memory
            memcpy(spr_memory, &memory[memory[0x4014] * 0x100], 0x100);
            break;

        case 0x2004: // OAMDATA: 1 byte transfer to sprite memory
            spr_memory[memory[0x2003]] = reg;
            memory[0x2003]++;
            break;

        case 0x2006: // PPUADDR: First write goes to latch
            ppu_latch_on = !ppu_latch_on;
            if (ppu_latch_on)
                ppu_latch = reg;
            break;

        case 0x2007: // PPUDATA: 1 byte transfer to PPU memory
            uint16_t ppu_address = (ppu_latch << 8) | memory[0x2006];

            // Mirror addresses
            uint16_t address_mirror = ppu_address % 0x4000;
            if (address_mirror >= 0x3000 && address_mirror < 0x3F00)
                address_mirror -= 0x1000;
            else if (address_mirror >= 0x3F20 && address_mirror < 0x4000)
                address_mirror = 0x3F00 + (address_mirror - 0x3F20) % 20;

            ppu_memory[address_mirror] = reg;

            // Increment the address
            ppu_address += (memory[0x2000] & 0x04) ? 32 : 1;
            ppu_latch = ppu_address >> 8;
            memory[0x2006] = ppu_address;

            break;
    }
}

void cpu() {
    // Handle interrupts
    for (int i = 0; i < 3; i++) {
        if (interrupts[i]) {
            ph_(--program_counter >> 8);
            ph_(program_counter);
            ph_(flags);
            se_(0x04);
            program_counter = memory[0xFFFB + i * 2] << 8 | memory[0xFFFA + i * 2];
            cycles += 7;
            interrupts[i] = false;
        }
    }

    // Decode opcode
    switch (memory[program_counter]) {
        case 0x69: adc(*immediate());   cycles += 2; break; // ADC immediate
        case 0x65: adc(*zero_page());   cycles += 3; break; // ADC zero page
        case 0x75: adc(*zero_page_x()); cycles += 4; break; // ADC zero page X
        case 0x6D: adc(*absolute());    cycles += 4; break; // ADC absolute
        case 0x7D: adc(*absolute_x());  cycles += 4; break; // ADC absolute X
        case 0x79: adc(*absolute_y());  cycles += 4; break; // ADC absolute Y
        case 0x61: adc(*indirect_x());  cycles += 6; break; // ADC indirect X
        case 0x71: adc(*indirect_y());  cycles += 5; break; // ADC indirect Y

        case 0x29: _and(*immediate());   cycles += 2; break; // AND immediate
        case 0x25: _and(*zero_page());   cycles += 3; break; // AND zero page
        case 0x35: _and(*zero_page_x()); cycles += 4; break; // AND zero page X
        case 0x2D: _and(*absolute());    cycles += 4; break; // AND absolute
        case 0x3D: _and(*absolute_x());  cycles += 4; break; // AND absolute X
        case 0x39: _and(*absolute_y());  cycles += 4; break; // AND absolute Y
        case 0x21: _and(*indirect_x());  cycles += 6; break; // AND indirect X
        case 0x31: _and(*indirect_y());  cycles += 5; break; // AND indirect Y

        case 0x0A: asl(&accumulator);  cycles += 2; break; // ASL accumulator
        case 0x06: asl(zero_page());   cycles += 5; break; // ASL zero page
        case 0x16: asl(zero_page_x()); cycles += 6; break; // ASL zero page X
        case 0x0E: asl(absolute());    cycles += 6; break; // ASL absolute
        case 0x1E: asl(absolute_x());  cycles += 7; break; // ASL absolute X

        case 0x24: bit(*zero_page()); cycles += 3; break; // BIT zero page
        case 0x2C: bit(*absolute());  cycles += 4; break; // BIT absolute

        case 0x10: b__(!(flags & 0x80)); cycles += !(flags & 0x80) ? 3 : 2; break; // BPL
        case 0x30: b__( (flags & 0x80)); cycles +=  (flags & 0x80) ? 3 : 2; break; // BMI
        case 0x50: b__(!(flags & 0x40)); cycles += !(flags & 0x40) ? 3 : 2; break; // BVC
        case 0x70: b__( (flags & 0x40)); cycles +=  (flags & 0x40) ? 3 : 2; break; // BVS
        case 0x90: b__(!(flags & 0x01)); cycles += !(flags & 0x01) ? 3 : 2; break; // BCC
        case 0xB0: b__( (flags & 0x01)); cycles +=  (flags & 0x01) ? 3 : 2; break; // BCS
        case 0xD0: b__(!(flags & 0x02)); cycles += !(flags & 0x02) ? 3 : 2; break; // BNE
        case 0xF0: b__( (flags & 0x02)); cycles +=  (flags & 0x02) ? 3 : 2; break; // BEQ

        case 0x00: brk(); cycles += 7; break; // BRK

        case 0xC9: cp_(accumulator, *immediate());   cycles += 2; break; // CMP immediate
        case 0xC5: cp_(accumulator, *zero_page());   cycles += 3; break; // CMP zero page
        case 0xD5: cp_(accumulator, *zero_page_x()); cycles += 4; break; // CMP zero page X
        case 0xCD: cp_(accumulator, *absolute());    cycles += 4; break; // CMP absolute
        case 0xDD: cp_(accumulator, *absolute_x());  cycles += 4; break; // CMP absolute X
        case 0xD9: cp_(accumulator, *absolute_y());  cycles += 4; break; // CMP absolute Y
        case 0xC1: cp_(accumulator, *indirect_x());  cycles += 6; break; // CMP indirect X
        case 0xD1: cp_(accumulator, *indirect_y());  cycles += 5; break; // CMP indirect Y

        case 0xE0: cp_(register_x, *immediate()); cycles += 2; break; // CPX immediate
        case 0xE4: cp_(register_x, *zero_page()); cycles += 3; break; // CPX zero page
        case 0xEC: cp_(register_x, *absolute());  cycles += 4; break; // CPX absolute

        case 0xC0: cp_(register_y, *immediate()); cycles += 2; break; // CPY immediate
        case 0xC4: cp_(register_y, *zero_page()); cycles += 3; break; // CPY zero page
        case 0xCC: cp_(register_y, *absolute());  cycles += 4; break; // CPY absolute

        case 0xC6: de_(zero_page());   cycles += 5; break; // DEC zero page
        case 0xD6: de_(zero_page_x()); cycles += 6; break; // DEC zero page X
        case 0xCE: de_(absolute());    cycles += 6; break; // DEC absolute
        case 0xDE: de_(absolute_x());  cycles += 7; break; // DEC absolute X
        
        case 0x49: eor(*immediate());   cycles += 2; break; // EOR immediate
        case 0x45: eor(*zero_page());   cycles += 3; break; // EOR zero page
        case 0x55: eor(*zero_page_x()); cycles += 4; break; // EOR zero page X
        case 0x4D: eor(*absolute());    cycles += 4; break; // EOR absolute
        case 0x5D: eor(*absolute_x());  cycles += 4; break; // EOR absolute X
        case 0x59: eor(*absolute_y());  cycles += 4; break; // EOR absolute Y
        case 0x41: eor(*indirect_x());  cycles += 6; break; // EOR indirect X
        case 0x51: eor(*indirect_y());  cycles += 5; break; // EOR indirect Y

        case 0x18: cl_(0x01); cycles += 2; break; // CLC
        case 0x38: se_(0x01); cycles += 2; break; // SEC
        case 0x58: cl_(0x04); cycles += 2; break; // CLI
        case 0x78: se_(0x04); cycles += 2; break; // SEI
        case 0xB8: cl_(0x40); cycles += 2; break; // CLV
        case 0xD8: cl_(0x08); cycles += 2; break; // CLD
        case 0xF8: se_(0x08); cycles += 2; break; // SED

        case 0xE6: in_(zero_page());   cycles += 5; break; // INC zero page
        case 0xF6: in_(zero_page_x()); cycles += 6; break; // INC zero page X
        case 0xEE: in_(absolute());    cycles += 6; break; // INC absolute
        case 0xFE: in_(absolute_x());  cycles += 7; break; // INC absolute X

        case 0x4C: jmp(absolute()); cycles += 3; break; // JMP absolute
        case 0x6C: jmp(indirect()); cycles += 5; break; // JMP indirect

        case 0x20: jsr(absolute()); cycles += 6; break; // JSR absolute

        case 0xA9: ld_(&accumulator, immediate());   cycles += 2; break; // LDA immediate
        case 0xA5: ld_(&accumulator, zero_page());   cycles += 3; break; // LDA zero page
        case 0xB5: ld_(&accumulator, zero_page_x()); cycles += 4; break; // LDA zero page X
        case 0xAD: ld_(&accumulator, absolute());    cycles += 4; break; // LDA absolute
        case 0xBD: ld_(&accumulator, absolute_x());  cycles += 4; break; // LDA absolute X
        case 0xB9: ld_(&accumulator, absolute_y());  cycles += 4; break; // LDA absolute Y
        case 0xA1: ld_(&accumulator, indirect_x());  cycles += 6; break; // LDA indirect X
        case 0xB1: ld_(&accumulator, indirect_y());  cycles += 5; break; // LDA indirect Y

        case 0xA2: ld_(&register_x, immediate());   cycles += 2; break; // LDX immediate
        case 0xA6: ld_(&register_x, zero_page());   cycles += 2; break; // LDX zero page
        case 0xB6: ld_(&register_x, zero_page_y()); cycles += 2; break; // LDX zero page Y
        case 0xAE: ld_(&register_x, absolute());    cycles += 3; break; // LDX absolute
        case 0xBE: ld_(&register_x, absolute_y());  cycles += 3; break; // LDX absolute Y

        case 0xA0: ld_(&register_y, immediate());   cycles += 2; break; // LDY immediate
        case 0xA4: ld_(&register_y, zero_page());   cycles += 2; break; // LDY zero page
        case 0xB4: ld_(&register_y, zero_page_x()); cycles += 2; break; // LDY zero page X
        case 0xAC: ld_(&register_y, absolute());    cycles += 3; break; // LDY absolute
        case 0xBC: ld_(&register_y, absolute_x());  cycles += 3; break; // LDY absolute X

        case 0x4A: lsr(&accumulator);  cycles += 2; break; // LSR accumulator
        case 0x46: lsr(zero_page());   cycles += 5; break; // LSR zero page
        case 0x56: lsr(zero_page_x()); cycles += 6; break; // LSR zero page X
        case 0x4E: lsr(absolute());    cycles += 6; break; // LSR absolute
        case 0x5E: lsr(absolute_x());  cycles += 7; break; // LSR absolute X

        case 0xEA: cycles += 2; break; // NOP

        case 0x09: ora(*immediate());   cycles += 2; break; // ORA immediate
        case 0x05: ora(*zero_page());   cycles += 3; break; // ORA zero page
        case 0x15: ora(*zero_page_x()); cycles += 4; break; // ORA zero page X
        case 0x0D: ora(*absolute());    cycles += 4; break; // ORA absolute
        case 0x1D: ora(*absolute_x());  cycles += 4; break; // ORA absolute X
        case 0x19: ora(*absolute_y());  cycles += 4; break; // ORA absolute Y
        case 0x01: ora(*indirect_x());  cycles += 6; break; // ORA indirect X
        case 0x11: ora(*indirect_y());  cycles += 5; break; // ORA indirect Y

        case 0xAA: t__(&accumulator, &register_x); cycles += 2; break; // TAX
        case 0x8A: t__(&register_x, &accumulator); cycles += 2; break; // TXA
        case 0xCA: de_(&register_x);               cycles += 2; break; // DEX
        case 0xE8: in_(&register_x);               cycles += 2; break; // INX
        case 0xA8: t__(&accumulator, &register_y); cycles += 2; break; // TAY
        case 0x98: t__(&register_y, &accumulator); cycles += 2; break; // TYA
        case 0x88: de_(&register_y);               cycles += 2; break; // DEY
        case 0xC8: in_(&register_y);               cycles += 2; break; // INY

        case 0x2A: rol(&accumulator);  cycles += 2; break; // ROL accumulator
        case 0x26: rol(zero_page());   cycles += 5; break; // ROL zero page
        case 0x36: rol(zero_page_x()); cycles += 6; break; // ROL zero page X
        case 0x2E: rol(absolute());    cycles += 6; break; // ROL absolute
        case 0x3E: rol(absolute_x());  cycles += 7; break; // ROL absolute X

        case 0x6A: ror(&accumulator);  cycles += 2; break; // ROR accumulator
        case 0x66: ror(zero_page());   cycles += 5; break; // ROR zero page
        case 0x76: ror(zero_page_x()); cycles += 6; break; // ROR zero page X
        case 0x6E: ror(absolute());    cycles += 6; break; // ROR absolute
        case 0x7E: ror(absolute_x());  cycles += 7; break; // ROR absolute X

        case 0x40: rti(); cycles += 6; break; // RTI

        case 0x60: rts(); cycles += 6; break; // RTS

        case 0xE9: sbc(*immediate());   cycles += 2; break; // SBC immediate
        case 0xE5: sbc(*zero_page());   cycles += 3; break; // SBC zero page
        case 0xF5: sbc(*zero_page_x()); cycles += 4; break; // SBC zero page X
        case 0xED: sbc(*absolute());    cycles += 4; break; // SBC absolute
        case 0xFD: sbc(*absolute_x());  cycles += 4; break; // SBC absolute X
        case 0xF9: sbc(*absolute_y());  cycles += 4; break; // SBC absolute Y
        case 0xE1: sbc(*indirect_x());  cycles += 6; break; // SBC indirect X
        case 0xF1: sbc(*indirect_y());  cycles += 5; break; // SBC indirect Y

        case 0x85: st_(accumulator, zero_page());   cycles += 3; break; // STA zero page
        case 0x95: st_(accumulator, zero_page_x()); cycles += 4; break; // STA zero page X
        case 0x8D: st_(accumulator, absolute());    cycles += 4; break; // STA absolute
        case 0x9D: st_(accumulator, absolute_x());  cycles += 5; break; // STA absolute X
        case 0x99: st_(accumulator, absolute_y());  cycles += 5; break; // STA absolute Y
        case 0x81: st_(accumulator, indirect_x());  cycles += 6; break; // STA indirect X
        case 0x91: st_(accumulator, indirect_y());  cycles += 6; break; // STA indirect Y

        case 0x9A: t__(&register_x, &stack_pointer); cycles += 2; break; // TXS
        case 0xBA: t__(&stack_pointer, &register_x); cycles += 2; break; // TSX
        case 0x48: ph_(accumulator);                 cycles += 3; break; // PHA
        case 0x68: pl_(&accumulator);                cycles += 4; break; // PLA
        case 0x08: ph_(flags);                       cycles += 3; break; // PHP
        case 0x28: pl_(&flags);                      cycles += 4; break; // PLP

        case 0x86: st_(register_x, zero_page());   cycles += 3; break; // STX zero page
        case 0x96: st_(register_x, zero_page_y()); cycles += 4; break; // STX zero page Y
        case 0x8E: st_(register_x, absolute());    cycles += 4; break; // STX absolute

        case 0x84: st_(register_y, zero_page());   cycles += 3; break; // STY zero page
        case 0x94: st_(register_y, zero_page_x()); cycles += 4; break; // STY zero page X
        case 0x8C: st_(register_y, absolute());    cycles += 4; break; // STY absolute

        default:
            printf("Unknown opcode: 0x%X\n", memory[program_counter]);
            exit(1);
    }

    program_counter++;
    if (cycles > 1786840)
        cycles -= 1786840;
}

void draw() {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 240, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, framebuffer);
    glBegin(GL_QUADS);
    glTexCoord2i(1, 1); glVertex2f( 1, -1);
    glTexCoord2i(0, 1); glVertex2f(-1, -1);
    glTexCoord2i(0, 0); glVertex2f(-1,  1);
    glTexCoord2i(1, 0); glVertex2f( 1,  1);
    glEnd();
    glFlush();
}

void ppu() {
    // Determine how many PPU cycles to run based on how many CPU cycles ran
    uint32_t target_ppu_cycles;
    if (scanline_cycles > cycles)
        target_ppu_cycles = (cycles + 1786840 - scanline_cycles) * 3;
    else
        target_ppu_cycles = (cycles - scanline_cycles) * 3;

    while (target_ppu_cycles > ppu_cycles) {
        if (scanline >= 1 && scanline <= 240) { // Draw visible lines
            uint8_t y = scanline - 1;

            if (ppu_cycles >= 1 && ppu_cycles <= 256 && memory[0x2001] & 0x08) { // Draw the background
                uint8_t x = ppu_cycles - 1;
                uint16_t table_offset = 0x2000 + (memory[0x2000] & 0x03) * 0x0400;
                uint16_t pattern_offset = (memory[0x2000] & 0x10) << 8;
                uint16_t tile = pattern_offset + ppu_memory[table_offset + (y / 8) * 32 + x / 8] * 16;
                uint8_t bits_low = ppu_memory[tile + y % 8] & (0x80 >> (x % 8)) ? 0x01 : 0x00;
                bits_low |= ppu_memory[tile + y % 8 + 8] & (0x80 >> (x % 8)) ? 0x02 : 0x00;

                // Get the upper 2 bits of the palette index from the attribute table
                uint8_t bits_high = ppu_memory[table_offset + 0x03C0 + (y / 32) * 8 + x / 32];
                if ((x / 8) % 2 == 0 && (y / 8) % 2 == 0) // Top left
                    bits_high = (bits_high & 0x03) << 2;
                else if ((x / 8) % 2 == 1 && (y / 8) % 2 == 0) // Top right
                    bits_high = (bits_high & 0x0C) << 0;
                else if ((x / 8) % 2 == 0 && (y / 8) % 2 == 1) // Bottom left
                    bits_high = (bits_high & 0x30) >> 2;
                else // Bottom right
                    bits_high = (bits_high & 0xC0) >> 4;

                // Draw a pixel if it isn't covered
                if (framebuffer[y * 256 + x] == palette[ppu_memory[0x3F00]])
                    framebuffer[y * 256 + x] = palette[ppu_memory[0x3F00 | bits_high | bits_low]];
            }
            else if (ppu_cycles >= 257 && ppu_cycles <= 320 && memory[0x2001] & 0x10) { // Draw the sprites
                uint8_t *sprite = &spr_memory[(ppu_cycles - 257) * 4];
                uint8_t height = (memory[0x2000] & 0x20) ? 16 : 8;

                if (*sprite <= y && *sprite + height > y) {
                    uint8_t x = *(sprite + 3);
                    uint8_t y_sprite = ((y - *sprite) / 8) * 16 + (y - *sprite) % 8;
                    uint16_t pattern_offset = (height == 8) ? (memory[0x2000] & 0x08) << 1 : (*(sprite + 1) & 0x01) << 12;
                    uint16_t tile = pattern_offset + *(sprite + 1) * 16;
                    uint8_t bits_high = (*(sprite + 2) & 0x03) << 2;

                    // Draw a sprite line on the next line
                    for (int i = 0; i < 8; i++) {
                        uint8_t bits_low = ppu_memory[tile + y_sprite] & (0x80 >> i) ? 0x01 : 0x00;
                        bits_low |= ppu_memory[tile + y_sprite + 8] & (0x80 >> i) ? 0x02 : 0x00;

                        uint8_t offset = (*(sprite + 2) & 0x40) ? 8 - i : i;
                        framebuffer[(y + 1) * 256 + x + offset] = palette[ppu_memory[0x3F10 | bits_high | bits_low]];
                    }
                }
            }
        } else if (scanline == 242 && ppu_cycles == 1) { // Start the V-blank period
            memory[0x2002] |= 0x80;
            if (memory[0x2000] & 0x80)
                interrupts[0] = true;
        }

        ppu_cycles++;
        if (ppu_cycles == 341) {
            ppu_cycles = 0;
            target_ppu_cycles -= 341;
            scanline_cycles = cycles;
            scanline++;
        }

        if (scanline == 263) { // End of frame
            memory[0x2002] &= ~0x80;
            scanline = 0;

            draw();

            // Clear the framebuffer
            for (int i = 0; i < 256 * 240; i++)
                framebuffer[i] = palette[ppu_memory[0x3F00]];

            // Limit FPS to 60
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - timer;
            if (elapsed.count() < 1.0f / 60)
                usleep((1.0f / 60 - elapsed.count()) * 1000000);
            timer = std::chrono::steady_clock::now();
        }
    }
}

void loop() {
    cpu();
    ppu();
    glutPostRedisplay();
}

void key_down(unsigned char key, int x, int y) {
    for (int i = 0; i < 8; i++) {
        if (key == keymap[i])
            memory[0x4016] |= 1 << i;
    }
}

void key_up(unsigned char key, int x, int y) {
    for (int i = 0; i < 8; i++) {
        if (key == keymap[i])
            memory[0x4016] &= ~(1 << i);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify a ROM to load.\n");
        return 0;
    }

    rom = fopen(argv[1], "rb");
    if (!rom) {
        printf("Failed to open ROM!\n");
        return 0;
    }

    uint8_t header[0x10];
    fread(header, 1, 0x10, rom);

    // Verify the file format
    char filetype[4];
    memcpy(filetype, header, 3);
    filetype[3] = '\0';
    if (strcmp(filetype, "NES") != 0 || header[3] != 0x1A) {
        printf("Invalid ROM format!\n");
        return 0;
    }

    // Load the ROM trainer into memory
    if (header[6] & 0x04)
        fread(&memory[0x7000], 1, 0x200, rom);

    mapper_type = header[7] | (header[6] >> 4);
    rom_address = ftell(rom);

    // Load the ROM banks into memory
    switch (mapper_type) {
        case 0: // NROM: Mirror a single bank or load both banks
        case 3: // CNROM: Mirror a single bank or load both banks
            fread(&memory[0x8000], 1, 0x4000, rom);
            if (header[4] == 1)
                memcpy(&memory[0xC000], &memory[0x8000], 0x4000);
            else
                fread(&memory[0xC000], 1, 0x4000, rom);
            break;

        case 1: // MMC1: Load the first and last banks
        case 2: // UNROM: Load the first and last banks
            fread(&memory[0x8000], 1, 0x4000, rom);
            fseek(rom, (header[4] - 2) * 0x4000, SEEK_CUR);
            rom_address_last = ftell(rom);
            fread(&memory[0xC000], 1, 0x4000, rom);
            mapper_registers[1] = 0x04;
            break;

        default:
            printf("Unknown mapper type: %d\n", mapper_type);
            fclose(rom);
            return 1;
    }

    // Load the first VROM bank into PPU memory
    if (header[5]) {
        vrom_address = ftell(rom);
        fread(ppu_memory, 1, 0x2000, rom);
    }

    // Trigger the reset interrupt
    interrupts[1] = true;

    glutInit(&argc, argv);
    glutInitWindowSize(256, 240);
    glutCreateWindow("NoiES");
    glEnable(GL_TEXTURE_2D);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glutDisplayFunc(loop);
    glutKeyboardFunc(key_down);
    glutKeyboardUpFunc(key_up);
    glutMainLoop();

    fclose(rom);
    return 0;
}
