#include <chrono>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include "GL/glut.h"
#include "portaudio.h"

uint8_t memory[0x10000];
uint8_t ppu_memory[0x4000];
uint8_t spr_memory[0x100];

uint32_t cycles, target_cycles;
uint16_t program_counter;
uint8_t accumulator, register_x, register_y;
uint8_t flags = 0x24; // NV_BDIZC
uint8_t stack_pointer = 0xFF;
bool interrupts[3]; // NMI, RST, BRK

uint32_t ppu_cycles;
uint16_t scanline;
uint8_t scroll_x, scroll_y;
uint8_t sprite_count;
uint8_t mirror_type;
uint8_t ppu_buffer;
uint8_t ppu_latch;
bool ppu_latch_on;

uint8_t input_shift;

uint16_t apu_timers[2];
uint16_t pulses[2];
uint8_t duty_cycles[2];
uint8_t volumes[2];
int16_t wavelengths[2];

uint8_t mapper_type;
uint8_t mapper_register, mapper_latch, mapper_shift;
uint8_t irq_counter, irq_latch;
bool irq_enable, irq_reload;

FILE *rom, *save;
uint32_t rom_address, rom_address_last, vrom_address;

uint32_t framebuffer[256 * 240];
uint32_t display[256 * 240];
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
uint8_t *absolute_x(bool page_cycle) {
    uint16_t address = memory[++program_counter] | (memory[++program_counter] << 8);
    if (page_cycle && address / 0x100 != (address + register_x) / 0x100) // Page cross
        target_cycles += 3;
    return &memory[address + register_x];
}

// Absolute Y addressing: Add the Y register to the absolute address
uint8_t *absolute_y(bool page_cycle) {
    uint16_t address = memory[++program_counter] | (memory[++program_counter] << 8);
    if (page_cycle && address / 0x100 != (address + register_y) / 0x100) // Page cross
        target_cycles += 3;
    return &memory[address + register_y];
}

// Indirect addressing: Use the memory address stored at the absolute address
uint8_t *indirect() {
    uint16_t address = memory[++program_counter] | (memory[++program_counter] << 8);
    uint16_t address_2 = (((address + 1) & 0x00FF) == 0x00) ? address + 1 - 0x100 : address + 1;
    return &memory[memory[address] | (memory[address_2] << 8)];
}

// Indirect X addressing: Use the memory address stored at the zero page X address 
uint8_t *indirect_x() {
    uint8_t address_lower = memory[(memory[++program_counter] + register_x) % 0x0100];
    uint8_t address_upper = memory[(memory[program_counter] + register_x + 1) % 0x0100];
    return &memory[address_lower | (address_upper << 8)];
}

// Indirect Y addressing: Add the Y register to the memory address stored at the zero page address
uint8_t *indirect_y(bool page_cycle) {
    uint8_t address_lower = memory[memory[++program_counter]];
    uint8_t address_upper = memory[(memory[program_counter] + 1) % 0x100];
    uint16_t address = address_lower | (address_upper << 8);
    if (page_cycle && address / 0x100 != (address + register_y) / 0x100) // Page cross
        target_cycles += 3;
    return &memory[address + register_y];
}

// Immediate addressing: Get the value immediately after the current address
uint8_t *immediate() {
    return &memory[++program_counter];
}

// Get the real location of a mirrored address in memory
uint16_t memory_mirror(uint16_t address) {
    if (address >= 0x0800 && address < 0x2000)
        address = address % 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + (address - 0x2000) % 8;
    return address;
}

// Get the real location of a mirrored address in PPU memory
uint16_t ppu_memory_mirror(uint16_t address) {
    address %= 0x4000;
    if (address >= 0x3000 && address < 0x3F00)
        address -= 0x1000;
    else if (address >= 0x3F20 && address < 0x4000)
        address = 0x3F00 + (address - 0x3F20) % 20;
    else if (address >= 0x3F10 && address < 0x3F20 && address % 4 == 0)
        address -= 0x10;

    // Nametable mirroring
    switch (mirror_type) {
        case 0: // 1-screen A
            if (address >= 0x2400 && address < 0x3000)
                address -= ((address - 0x2000) / 0x0400) * 0x0400;
            break;

        case 1: // 1-screen B
            if (address >= 0x2000 && address < 0x2400)
                address += 0x0400;
            else if (address >= 0x2800 && address < 0x3000)
                address -= ((address - 0x2400) / 0x0400) * 0x0400;
            break;

        case 2: // Vertical
            if (address >= 0x2800 && address < 0x3000)
                address -= 0x0800;
            break;

        case 3: // Horizontal
            if (address >= 0x2400 && address < 0x2800)
                address -= 0x0400;
            else if (address >= 0x2800 && address < 0x3000)
                address -= ((address - 0x2400) / 0x0400) * 0x0400;
            break;
    }

    return address;
}

// Handle writes to mapper registers
void mapper_write(uint16_t address, uint8_t value) {
    switch (mapper_type) {
        case 1: // MMC1: Swap 16 KB or 32 KB ROM banks and 4 KB or 8 KB VROM banks
            if (value & 0x80) {
                // Reset the shift register on a write with bit 7 set
                mapper_latch |= 0x0C;
                mapper_shift = 0;
            }
            else {
                // Write one bit to the latch
                mapper_latch |= (value & 0x01) << mapper_shift;
                mapper_shift++;
            }

            if (mapper_shift == 5) {
                if (address >= 0x8000 && address < 0xA000) { // Control
                    mapper_register = mapper_latch;
                    mirror_type = mapper_latch & 0x03;
                }
                else if (address >= 0xA000 && address < 0xC000) { // VROM bank 0
                    if (mapper_register & 0x10) { // 4 KB
                        fseek(rom, vrom_address + 0x1000 * mapper_latch, SEEK_SET);
                        fread(ppu_memory, 1, 0x1000, rom);
                    }
                    else { // 8 KB
                        fseek(rom, vrom_address + 0x1000 * (mapper_latch & ~0x01), SEEK_SET);
                        fread(ppu_memory, 1, 0x2000, rom);
                    }
                }
                else if (address >= 0xC000 && address < 0xE000) { // VROM bank 1
                    if (mapper_register & 0x10) { // 4 KB
                        fseek(rom, vrom_address + 0x1000 * mapper_latch, SEEK_SET);
                        fread(&ppu_memory[0x1000], 1, 0x1000, rom);
                    }
                }
                else { // ROM banks
                    if (mapper_register & 0x04) { // ROM bank 1 is fixed
                        if (mapper_register & 0x08) { // 16 KB
                            fseek(rom, rom_address + 0x4000 * mapper_latch, SEEK_SET);
                            fread(&memory[0x8000], 1, 0x4000, rom);
                        }
                        else { // 32 KB
                            fseek(rom, rom_address + 0x4000 * (mapper_latch & 0x0E), SEEK_SET);
                            fread(&memory[0x8000], 1, 0x8000, rom);
                        }

                        fseek(rom, rom_address_last, SEEK_SET);
                        fread(&memory[0xC000], 1, 0x4000, rom);
                    }
                    else { // ROM bank 0 is fixed
                        if (mapper_register & 0x08) { // 16 KB
                            fseek(rom, rom_address + 0x4000 * mapper_latch, SEEK_SET);
                            fread(&memory[0xC000], 1, 0x4000, rom);
                        }
                        else { // 32 KB
                            fseek(rom, rom_address + 0x4000 * (mapper_latch & 0x0E), SEEK_SET);
                            fread(&memory[0x8000], 1, 0x8000, rom);
                        }

                        fseek(rom, rom_address, SEEK_SET);
                        fread(&memory[0x8000], 1, 0x4000, rom);
                    }
                }

                mapper_latch = 0;
                mapper_shift = 0;
            }

            break;

        case 2: // UNROM: Swap the first ROM bank only
            fseek(rom, rom_address + 0x4000 * value, SEEK_SET);
            fread(&memory[0x8000], 1, 0x4000, rom);
            break;

        case 3: // CNROM: Swap the VROM bank only
            fseek(rom, vrom_address + 0x2000 * (value & 0x03), SEEK_SET);
            fread(ppu_memory, 1, 0x2000, rom);
            break;

        case 4: // MMC3: Swap 8 KB ROM banks and 1 KB or 2 KB VROM banks
            if (address >= 0x8000 && address < 0xA000) {
                if (address % 2 == 0) { // Bank select
                    mapper_register = value;
                    fseek(rom, rom_address_last, SEEK_SET);
                    fread(&memory[(value & 0x40) ? 0x8000 : 0xC000], 1, 0x2000, rom);
                }
                else { // Bank data
                    uint8_t bank = mapper_register & 0x07;
                    if (bank < 2) { // 2 KB VROM banks
                        fseek(rom, vrom_address + 0x0400 * (value & ~0x01), SEEK_SET);
                        fread(&ppu_memory[((mapper_register & 0x80) << 5) + 0x0800 * bank], 1, 0x0800, rom);
                    }
                    else if (bank >= 2 && bank < 6) { // 1 KB VROM banks
                        fseek(rom, vrom_address + 0x0400 * value, SEEK_SET);
                        fread(&ppu_memory[(!(mapper_register & 0x80) << 12) + 0x0400 * (bank - 2)], 1, 0x0400, rom);
                    }
                    else if (bank == 6) { // Swappable/fixed 8 KB ROM bank
                        fseek(rom, rom_address + 0x2000 * value, SEEK_SET);
                        fread(&memory[(mapper_register & 0x40) ? 0xC000 : 0x8000], 1, 0x2000, rom);
                    }
                    else { // Swappable 8 KB ROM bank
                        fseek(rom, rom_address + 0x2000 * value, SEEK_SET);
                        fread(&memory[0xA000], 1, 0x2000, rom);
                    }
                }
            }
            else if (address >= 0xA000 && address < 0xC000) {
                if (address % 2 == 0) { // Mirroring
                    if (mirror_type != 4)
                        mirror_type = 2 + value;
                }
            }
            else if (address >= 0xC000 && address < 0xE000) {
                if (address % 2 == 0) { // IRQ latch
                    irq_latch = value;
                }
                else { // IRQ reload
                    irq_counter = 0;
                    irq_reload = true;
                }
            }
            else { // IRQ toggle
                irq_enable = (address % 2 == 1);
            }
            break;
    }
}

// CL_: Clear a flag
void cl_(uint8_t flag) {
    flags &= ~flag;
}

// SE_: Set a flag
void se_(uint8_t flag) {
    flags |= flag;
}

// PH_: Push a value to the stack
void ph_(uint8_t value) {
    memory[0x0100 + stack_pointer--] = value;
}

// PLA: Pull the accumulator from the stack
void pla() {
    accumulator = memory[0x0100 + ++stack_pointer];

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// PLP: Pull the flags from the stack
void plp() {
    flags = memory[0x0100 + ++stack_pointer];

    se_(0x20);
    cl_(0x10);
}

// ADC: Add with carry
void adc(uint8_t value) {
    uint8_t accum_old = accumulator;
    accumulator += value + (flags & 0x01);

    if (accumulator & 0x80)                                                       se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                         se_(0x02); else cl_(0x02); // Z
    if ((value & 0x80) == (accum_old & 0x80) && (flags & 0x80) != (value & 0x80)) se_(0x40); else cl_(0x40); // V
    if (accum_old > accumulator || value + (flags & 0x01) == 0x100)               se_(0x01); else cl_(0x01); // C
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
    if (value & 0x80)               se_(0x80); else cl_(0x80); // N
    if (value & 0x40)               se_(0x40); else cl_(0x40); // V
    if ((accumulator & value) == 0) se_(0x02); else cl_(0x02); // Z
}

// B__: Branch on condition
void b__(bool condition) {
    int8_t value = *immediate();
    if (condition) {
        program_counter += value;
        target_cycles += 3;
        if ((program_counter + 1) / 0x100 != (program_counter - value) / 0x100) // Page cross
            target_cycles += 3;
    }
}

// BRK: Break
void brk() {
    if (!(flags & 0x04)) {
        se_(0x10);
        interrupts[2] = true;
    }
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
    uint16_t address = memory_mirror(value - memory);
    *reg = memory[address];

    if (*reg & 0x80) se_(0x80); else cl_(0x80); // N
    if (*reg == 0)   se_(0x02); else cl_(0x02); // Z

    // Handle I/O and PPU registers
    switch (address) {
        case 0x4016: // JOYPAD1: Read button status 1 bit at a time
            *reg = (memory[0x4016] & (1 << input_shift)) ? 0x41 : 0x40;
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

            // Buffer non-palette reads
            if (ppu_address < 0x3F00)
                *reg = ppu_buffer;
            else
                *reg = ppu_memory[ppu_memory_mirror(ppu_address)];

            ppu_buffer = ppu_memory[ppu_memory_mirror(ppu_address)];

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

// TXS: Transfer the X register to the stack pointer
void txs() {
    stack_pointer = register_x;
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

// RTS: Return from subroutine
void rts() {
    program_counter = memory[0x0100 + ++stack_pointer] | (memory[0x0100 + ++stack_pointer] << 8);
}

// RTI: Return from interrupt
void rti() {
    plp();
    rts();
    program_counter--;
}

// SBC: Subtract with carry
void sbc(uint8_t value) {
    uint8_t accum_old = accumulator;
    accumulator -= value + !(flags & 0x01);

    if (accumulator & 0x80)                                                       se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                         se_(0x02); else cl_(0x02); // Z
    if ((value & 0x80) != (accum_old & 0x80) && (flags & 0x80) == (value & 0x80)) se_(0x40); else cl_(0x40); // V
    if (accum_old >= accumulator && value + !(flags & 0x01) != 0x100)             se_(0x01); else cl_(0x01); // C
}

// ST_: Store a register
void st_(uint8_t reg, uint8_t *dst) {
    uint16_t address = memory_mirror(dst - memory);

    if (address >= 0x8000)
        mapper_write(address, reg);
    else if (address != 0x2002 && address != 0x4016)
        memory[address] = reg;

    // Handle PPU registers
    switch (address) {
        case 0x4014: // OAMDMA: DMA transfer to sprite memory
            memcpy(spr_memory, &memory[memory[0x4014] * 0x100], 0x100);
            target_cycles += ((target_cycles / 3) % 2) ? 1542 : 1539;
            break;

        case 0x2004: // OAMDATA: 1 byte transfer to sprite memory
            spr_memory[memory[0x2003]] = reg;
            memory[0x2003]++;
            break;

        case 0x2005: // PPUSCROLL: Second write sets the scroll positions
            ppu_latch_on = !ppu_latch_on;
            if (ppu_latch_on) {
                ppu_latch = reg;
            }
            else {
                scroll_x = ppu_latch;
                scroll_y = reg;
            }
            break;

        case 0x2006: // PPUADDR: First write goes to the latch
            ppu_latch_on = !ppu_latch_on;
            if (ppu_latch_on)
                ppu_latch = reg;
            break;

        case 0x2007: // PPUDATA: 1 byte transfer to PPU memory
            uint16_t ppu_address = (ppu_latch << 8) | memory[0x2006];

            ppu_memory[ppu_memory_mirror(ppu_address)] = reg;

            // Increment the address
            ppu_address += (memory[0x2000] & 0x04) ? 32 : 1;
            ppu_latch = ppu_address >> 8;
            memory[0x2006] = ppu_address;

            break;
    }
}

void cpu() {
    cycles++;
    if (cycles < target_cycles)
        return;

    // Reset the cycle counter every second
    if (target_cycles > 5360520) {
        target_cycles -= 5360520;
        cycles -= 5360520;
    }

    // Handle interrupts
    for (int i = 0; i < 3; i++) {
        if (interrupts[i]) {
            ph_(program_counter >> 8);
            ph_(program_counter);
            ph_(flags);
            cl_(0x10);
            se_(0x04);
            program_counter = memory[0xFFFB + i * 2] << 8 | memory[0xFFFA + i * 2];
            target_cycles += 21;
            interrupts[i] = false;
        }
    }

    // Decode opcode
    switch (memory[program_counter]) {
        case 0x69: adc(*immediate());      target_cycles += 6;  break; // ADC immediate
        case 0x65: adc(*zero_page());      target_cycles += 9;  break; // ADC zero page
        case 0x75: adc(*zero_page_x());    target_cycles += 12; break; // ADC zero page X
        case 0x6D: adc(*absolute());       target_cycles += 12; break; // ADC absolute
        case 0x7D: adc(*absolute_x(true)); target_cycles += 12; break; // ADC absolute X
        case 0x79: adc(*absolute_y(true)); target_cycles += 12; break; // ADC absolute Y
        case 0x61: adc(*indirect_x());     target_cycles += 18; break; // ADC indirect X
        case 0x71: adc(*indirect_y(true)); target_cycles += 15; break; // ADC indirect Y

        case 0x29: _and(*immediate());      target_cycles += 6;  break; // AND immediate
        case 0x25: _and(*zero_page());      target_cycles += 9;  break; // AND zero page
        case 0x35: _and(*zero_page_x());    target_cycles += 12; break; // AND zero page X
        case 0x2D: _and(*absolute());       target_cycles += 12; break; // AND absolute
        case 0x3D: _and(*absolute_x(true)); target_cycles += 12; break; // AND absolute X
        case 0x39: _and(*absolute_y(true)); target_cycles += 12; break; // AND absolute Y
        case 0x21: _and(*indirect_x());     target_cycles += 18; break; // AND indirect X
        case 0x31: _and(*indirect_y(true)); target_cycles += 15; break; // AND indirect Y

        case 0x0A: asl(&accumulator);       target_cycles += 6;  break; // ASL accumulator
        case 0x06: asl(zero_page());        target_cycles += 15; break; // ASL zero page
        case 0x16: asl(zero_page_x());      target_cycles += 18; break; // ASL zero page X
        case 0x0E: asl(absolute());         target_cycles += 18; break; // ASL absolute
        case 0x1E: asl(absolute_x(false));  target_cycles += 21; break; // ASL absolute X

        case 0x24: bit(*zero_page()); target_cycles += 9;  break; // BIT zero page
        case 0x2C: bit(*absolute());  target_cycles += 12; break; // BIT absolute

        case 0x10: b__(!(flags & 0x80)); target_cycles += 6; break; // BPL
        case 0x30: b__( (flags & 0x80)); target_cycles += 6; break; // BMI
        case 0x50: b__(!(flags & 0x40)); target_cycles += 6; break; // BVC
        case 0x70: b__( (flags & 0x40)); target_cycles += 6; break; // BVS
        case 0x90: b__(!(flags & 0x01)); target_cycles += 6; break; // BCC
        case 0xB0: b__( (flags & 0x01)); target_cycles += 6; break; // BCS
        case 0xD0: b__(!(flags & 0x02)); target_cycles += 6; break; // BNE
        case 0xF0: b__( (flags & 0x02)); target_cycles += 6; break; // BEQ

        case 0x00: brk(); target_cycles += 21; break; // BRK

        case 0xC9: cp_(accumulator, *immediate());      target_cycles += 6;  break; // CMP immediate
        case 0xC5: cp_(accumulator, *zero_page());      target_cycles += 9;  break; // CMP zero page
        case 0xD5: cp_(accumulator, *zero_page_x());    target_cycles += 12; break; // CMP zero page X
        case 0xCD: cp_(accumulator, *absolute());       target_cycles += 12; break; // CMP absolute
        case 0xDD: cp_(accumulator, *absolute_x(true)); target_cycles += 12; break; // CMP absolute X
        case 0xD9: cp_(accumulator, *absolute_y(true)); target_cycles += 12; break; // CMP absolute Y
        case 0xC1: cp_(accumulator, *indirect_x());     target_cycles += 18; break; // CMP indirect X
        case 0xD1: cp_(accumulator, *indirect_y(true)); target_cycles += 15; break; // CMP indirect Y

        case 0xE0: cp_(register_x, *immediate()); target_cycles += 6;  break; // CPX immediate
        case 0xE4: cp_(register_x, *zero_page()); target_cycles += 9;  break; // CPX zero page
        case 0xEC: cp_(register_x, *absolute());  target_cycles += 12; break; // CPX absolute

        case 0xC0: cp_(register_y, *immediate()); target_cycles += 6;  break; // CPY immediate
        case 0xC4: cp_(register_y, *zero_page()); target_cycles += 9;  break; // CPY zero page
        case 0xCC: cp_(register_y, *absolute());  target_cycles += 12; break; // CPY absolute

        case 0xC6: de_(zero_page());       target_cycles += 15; break; // DEC zero page
        case 0xD6: de_(zero_page_x());     target_cycles += 18; break; // DEC zero page X
        case 0xCE: de_(absolute());        target_cycles += 18; break; // DEC absolute
        case 0xDE: de_(absolute_x(false)); target_cycles += 21; break; // DEC absolute X
        
        case 0x49: eor(*immediate());      target_cycles += 6;  break; // EOR immediate
        case 0x45: eor(*zero_page());      target_cycles += 9;  break; // EOR zero page
        case 0x55: eor(*zero_page_x());    target_cycles += 12; break; // EOR zero page X
        case 0x4D: eor(*absolute());       target_cycles += 12; break; // EOR absolute
        case 0x5D: eor(*absolute_x(true)); target_cycles += 12; break; // EOR absolute X
        case 0x59: eor(*absolute_y(true)); target_cycles += 12; break; // EOR absolute Y
        case 0x41: eor(*indirect_x());     target_cycles += 18; break; // EOR indirect X
        case 0x51: eor(*indirect_y(true)); target_cycles += 15; break; // EOR indirect Y

        case 0x18: cl_(0x01); target_cycles += 6; break; // CLC
        case 0x38: se_(0x01); target_cycles += 6; break; // SEC
        case 0x58: cl_(0x04); target_cycles += 6; break; // CLI
        case 0x78: se_(0x04); target_cycles += 6; break; // SEI
        case 0xB8: cl_(0x40); target_cycles += 6; break; // CLV
        case 0xD8: cl_(0x08); target_cycles += 6; break; // CLD
        case 0xF8: se_(0x08); target_cycles += 6; break; // SED

        case 0xE6: in_(zero_page());       target_cycles += 15; break; // INC zero page
        case 0xF6: in_(zero_page_x());     target_cycles += 18; break; // INC zero page X
        case 0xEE: in_(absolute());        target_cycles += 18; break; // INC absolute
        case 0xFE: in_(absolute_x(false)); target_cycles += 21; break; // INC absolute X

        case 0x4C: jmp(absolute()); target_cycles += 9;  break; // JMP absolute
        case 0x6C: jmp(indirect()); target_cycles += 15; break; // JMP indirect

        case 0x20: jsr(absolute()); target_cycles += 18; break; // JSR absolute

        case 0xA9: ld_(&accumulator, immediate());      target_cycles += 6;  break; // LDA immediate
        case 0xA5: ld_(&accumulator, zero_page());      target_cycles += 9;  break; // LDA zero page
        case 0xB5: ld_(&accumulator, zero_page_x());    target_cycles += 12; break; // LDA zero page X
        case 0xAD: ld_(&accumulator, absolute());       target_cycles += 12; break; // LDA absolute
        case 0xBD: ld_(&accumulator, absolute_x(true)); target_cycles += 12; break; // LDA absolute X
        case 0xB9: ld_(&accumulator, absolute_y(true)); target_cycles += 12; break; // LDA absolute Y
        case 0xA1: ld_(&accumulator, indirect_x());     target_cycles += 18; break; // LDA indirect X
        case 0xB1: ld_(&accumulator, indirect_y(true)); target_cycles += 15; break; // LDA indirect Y

        case 0xA2: ld_(&register_x, immediate());      target_cycles += 6; break; // LDX immediate
        case 0xA6: ld_(&register_x, zero_page());      target_cycles += 6; break; // LDX zero page
        case 0xB6: ld_(&register_x, zero_page_y());    target_cycles += 6; break; // LDX zero page Y
        case 0xAE: ld_(&register_x, absolute());       target_cycles += 9; break; // LDX absolute
        case 0xBE: ld_(&register_x, absolute_y(true)); target_cycles += 9; break; // LDX absolute Y

        case 0xA0: ld_(&register_y, immediate());      target_cycles += 6; break; // LDY immediate
        case 0xA4: ld_(&register_y, zero_page());      target_cycles += 6; break; // LDY zero page
        case 0xB4: ld_(&register_y, zero_page_x());    target_cycles += 6; break; // LDY zero page X
        case 0xAC: ld_(&register_y, absolute());       target_cycles += 9; break; // LDY absolute
        case 0xBC: ld_(&register_y, absolute_x(true)); target_cycles += 9; break; // LDY absolute X

        case 0x4A: lsr(&accumulator);      target_cycles += 6;  break; // LSR accumulator
        case 0x46: lsr(zero_page());       target_cycles += 15; break; // LSR zero page
        case 0x56: lsr(zero_page_x());     target_cycles += 18; break; // LSR zero page X
        case 0x4E: lsr(absolute());        target_cycles += 18; break; // LSR absolute
        case 0x5E: lsr(absolute_x(false)); target_cycles += 21; break; // LSR absolute X

        case 0xEA: target_cycles += 6; break; // NOP

        case 0x09: ora(*immediate());      target_cycles += 6;  break; // ORA immediate
        case 0x05: ora(*zero_page());      target_cycles += 9;  break; // ORA zero page
        case 0x15: ora(*zero_page_x());    target_cycles += 12; break; // ORA zero page X
        case 0x0D: ora(*absolute());       target_cycles += 12; break; // ORA absolute
        case 0x1D: ora(*absolute_x(true)); target_cycles += 12; break; // ORA absolute X
        case 0x19: ora(*absolute_y(true)); target_cycles += 12; break; // ORA absolute Y
        case 0x01: ora(*indirect_x());     target_cycles += 18; break; // ORA indirect X
        case 0x11: ora(*indirect_y(true)); target_cycles += 15; break; // ORA indirect Y

        case 0xAA: t__(&accumulator, &register_x); target_cycles += 6; break; // TAX
        case 0x8A: t__(&register_x, &accumulator); target_cycles += 6; break; // TXA
        case 0xCA: de_(&register_x);               target_cycles += 6; break; // DEX
        case 0xE8: in_(&register_x);               target_cycles += 6; break; // INX
        case 0xA8: t__(&accumulator, &register_y); target_cycles += 6; break; // TAY
        case 0x98: t__(&register_y, &accumulator); target_cycles += 6; break; // TYA
        case 0x88: de_(&register_y);               target_cycles += 6; break; // DEY
        case 0xC8: in_(&register_y);               target_cycles += 6; break; // INY

        case 0x2A: rol(&accumulator);      target_cycles += 6;  break; // ROL accumulator
        case 0x26: rol(zero_page());       target_cycles += 15; break; // ROL zero page
        case 0x36: rol(zero_page_x());     target_cycles += 18; break; // ROL zero page X
        case 0x2E: rol(absolute());        target_cycles += 18; break; // ROL absolute
        case 0x3E: rol(absolute_x(false)); target_cycles += 21; break; // ROL absolute X

        case 0x6A: ror(&accumulator);      target_cycles += 6;  break; // ROR accumulator
        case 0x66: ror(zero_page());       target_cycles += 15; break; // ROR zero page
        case 0x76: ror(zero_page_x());     target_cycles += 18; break; // ROR zero page X
        case 0x6E: ror(absolute());        target_cycles += 18; break; // ROR absolute
        case 0x7E: ror(absolute_x(false)); target_cycles += 21; break; // ROR absolute X

        case 0x40: rti(); target_cycles += 18; break; // RTI

        case 0x60: rts(); target_cycles += 18; break; // RTS

        case 0xE9: sbc(*immediate());      target_cycles += 6;  break; // SBC immediate
        case 0xE5: sbc(*zero_page());      target_cycles += 9;  break; // SBC zero page
        case 0xF5: sbc(*zero_page_x());    target_cycles += 12; break; // SBC zero page X
        case 0xED: sbc(*absolute());       target_cycles += 12; break; // SBC absolute
        case 0xFD: sbc(*absolute_x(true)); target_cycles += 12; break; // SBC absolute X
        case 0xF9: sbc(*absolute_y(true)); target_cycles += 12; break; // SBC absolute Y
        case 0xE1: sbc(*indirect_x());     target_cycles += 18; break; // SBC indirect X
        case 0xF1: sbc(*indirect_y(true)); target_cycles += 15; break; // SBC indirect Y

        case 0x85: st_(accumulator, zero_page());       target_cycles += 9;  break; // STA zero page
        case 0x95: st_(accumulator, zero_page_x());     target_cycles += 12; break; // STA zero page X
        case 0x8D: st_(accumulator, absolute());        target_cycles += 12; break; // STA absolute
        case 0x9D: st_(accumulator, absolute_x(false)); target_cycles += 15; break; // STA absolute X
        case 0x99: st_(accumulator, absolute_y(false)); target_cycles += 15; break; // STA absolute Y
        case 0x81: st_(accumulator, indirect_x());      target_cycles += 18; break; // STA indirect X
        case 0x91: st_(accumulator, indirect_y(false)); target_cycles += 18; break; // STA indirect Y

        case 0x9A: txs();                            target_cycles += 6;  break; // TXS
        case 0xBA: t__(&stack_pointer, &register_x); target_cycles += 6;  break; // TSX
        case 0x48: ph_(accumulator);                 target_cycles += 9;  break; // PHA
        case 0x68: pla();                            target_cycles += 12; break; // PLA
        case 0x08: ph_(flags | 0x10);                target_cycles += 9;  break; // PHP
        case 0x28: plp();                            target_cycles += 12; break; // PLP

        case 0x86: st_(register_x, zero_page());   target_cycles += 9;  break; // STX zero page
        case 0x96: st_(register_x, zero_page_y()); target_cycles += 12; break; // STX zero page Y
        case 0x8E: st_(register_x, absolute());    target_cycles += 12; break; // STX absolute

        case 0x84: st_(register_y, zero_page());   target_cycles += 9;  break; // STY zero page
        case 0x94: st_(register_y, zero_page_x()); target_cycles += 12; break; // STY zero page X
        case 0x8C: st_(register_y, absolute());    target_cycles += 12; break; // STY absolute

        default:
            printf("Unknown opcode: 0x%X\n", memory[program_counter]);
            break;
    }

    program_counter++;
}

void ppu() {
    if (scanline >= 0 && scanline < 240) { // Visible lines
        if (ppu_cycles >= 1 && ppu_cycles <= 256 && memory[0x2001] & 0x08) { // Background drawing
            uint16_t x = ppu_cycles - 1;
            uint16_t x_offset = x + scroll_x;
            uint16_t y_offset = scanline + scroll_y;
            uint16_t table_offset = 0x2000 + (memory[0x2000] & 0x03) * 0x0400;
            uint16_t pattern_offset = (memory[0x2000] & 0x10) << 8;

            // Change nametable based on the scroll offset
            if (x_offset >= 256) {
                table_offset += 0x0400;
                x_offset %= 256;
            }
            if (y_offset >= 240) {
                table_offset += 0x0800;
                y_offset %= 240;
            }
            table_offset = ppu_memory_mirror(table_offset);

            uint16_t tile = pattern_offset + ppu_memory[table_offset + (y_offset / 8) * 32 + x_offset / 8] * 16;
            uint8_t bits_low = ppu_memory[tile + y_offset % 8] & (0x80 >> (x_offset % 8)) ? 0x01 : 0x00;
            bits_low |= ppu_memory[tile + y_offset % 8 + 8] & (0x80 >> (x_offset % 8)) ? 0x02 : 0x00;

            // Get the upper 2 bits of the palette index from the attribute table
            uint8_t bits_high = ppu_memory[table_offset + 0x03C0 + (y_offset / 32) * 8 + x_offset / 32];
            if ((x_offset / 16) % 2 == 0 && (y_offset / 16) % 2 == 0) // Top left
                bits_high = (bits_high & 0x03) << 2;
            else if ((x_offset / 16) % 2 == 1 && (y_offset / 16) % 2 == 0) // Top right
                bits_high = (bits_high & 0x0C) << 0;
            else if ((x_offset / 16) % 2 == 0 && (y_offset / 16) % 2 == 1) // Bottom left
                bits_high = (bits_high & 0x30) >> 2;
            else // Bottom right
                bits_high = (bits_high & 0xC0) >> 4;

            if ((x >= 8 || memory[0x2001] & 0x02) && bits_low != 0) {
                uint8_t type = framebuffer[scanline * 256 + x];

                // Check for a sprite 0 hit
                if (type < 0xFD)
                    memory[0x2002] |= 0x40;

                // Draw a pixel
                if (type % 2 == 1)
                    framebuffer[scanline * 256 + x] = palette[ppu_memory[0x3F00 | bits_high | bits_low]];
            }
        }
        else if (ppu_cycles >= 257 && ppu_cycles <= 320 && memory[0x2001] & 0x10) { // Sprite drawing
            uint8_t *sprite = &spr_memory[(ppu_cycles - 257) * 4];
            uint8_t height = (memory[0x2000] & 0x20) ? 16 : 8;

            if (*sprite <= scanline && *sprite + height > scanline) {
                if (sprite_count < 8) {
                    uint8_t x = *(sprite + 3);
                    uint8_t y_sprite = ((scanline - *sprite) / 8) * 16 + (scanline - *sprite) % 8;
                    uint16_t pattern_offset = (height == 8) ? (memory[0x2000] & 0x08) << 9 : (*(sprite + 1) & 0x01) << 12;
                    uint16_t tile = pattern_offset + (*(sprite + 1) & (height == 8 ? ~0x00 : ~0x01)) * 16;
                    uint8_t bits_high = (*(sprite + 2) & 0x03) << 2;

                    if (*(sprite + 2) & 0x80)
                        y_sprite = 7 - y_sprite;

                    // Draw a sprite line on the next line
                    for (int i = 0; i < 8; i++) {
                        uint16_t x_offset = x + ((*(sprite + 2) & 0x40) ? 7 - i : i);
                        uint8_t bits_low = ppu_memory[tile + y_sprite] & (0x80 >> i) ? 0x01 : 0x00;
                        bits_low |= ppu_memory[tile + y_sprite + 8] & (0x80 >> i) ? 0x02 : 0x00;

                        if (x_offset < 256 && (x_offset >= 8 || memory[0x2001] & 0x04) && bits_low != 0) {
                            framebuffer[(scanline + 1) * 256 + x_offset] = palette[ppu_memory[0x3F10 | bits_high | bits_low]];

                            // Mark opaque pixels
                            framebuffer[(scanline + 1) * 256 + x_offset]--;
                            if (*(sprite + 2) & 0x20) // Sprite is behind the background
                                framebuffer[(scanline + 1) * 256 + x_offset]--;
                            if (ppu_cycles == 257) // Sprite 0
                                framebuffer[(scanline + 1) * 256 + x_offset] -= 2;
                        }
                    }

                    sprite_count++;
                }
                else { // Sprite overflow
                    memory[0x2002] |= 0x20;
                }
            }
        }

        // MMC3 IRQ counter
        if (mapper_type == 4 && ppu_cycles == 260 && memory[0x2001] & 0x08 && memory[0x2001] & 0x10) {
            if (irq_counter == 0) {
                // Trigger an IRQ if they're enabled
                if (irq_enable && !irq_reload)
                    interrupts[2] = true;

                // Reload the counter
                irq_counter = irq_latch;
                irq_reload = false;
            }
            else {
                irq_counter--;
            }
        }
    }
    else if (scanline == 241 && ppu_cycles == 1) { // Start of the V-blank period
        memory[0x2002] |= 0x80;
        if (memory[0x2000] & 0x80)
            interrupts[0] = true;
    }
    else if (scanline == 261 && ppu_cycles == 1) { // Pre-render line
        // Clear the bits for the next frame
        memory[0x2002] &= ~0xE0;
    }

    ppu_cycles++;

    if (ppu_cycles == 341) { // End of a scanline
        sprite_count = 0;
        ppu_cycles = 0;
        scanline++;
    }

    if (scanline == 262) { // End of a frame
        scanline = 0;

        // Copy the finished frame to the display
        memcpy(display, framebuffer, sizeof(display));

        // Clear the framebuffer
        for (int i = 0; i < 256 * 240; i++)
            framebuffer[i] = palette[ppu_memory[0x3F00]];

        // Limit the FPS to 60
        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - timer;
        if (elapsed.count() < 1.0f / 60)
            usleep((1.0f / 60 - elapsed.count()) * 1000000);
        timer = std::chrono::steady_clock::now();
    }
}

void apu() {
    if (cycles % 6 == 0) { // Every other CPU cycle
        // Decrement and reload the pulse channel timers
        for (int i = 0; i < 2; i++) {
            if (apu_timers[i] == 0) {
                apu_timers[i] = pulses[i] = ((memory[0x4003 + 4 * i] & 0x03) << 8) | memory[0x4002 + 4 * i];
                duty_cycles[i] = (memory[0x4004 + 4 * i] & 0xC0) >> 6;
                volumes[i] = (memory[0x4000 + 4 * i] & 0x10) ? (memory[0x4000 + 4 * i] & 0x0F) : 7;
            }
            else {
                apu_timers[i]--;
            }
        }
    }
}

void run() {
    while (true) {
        cpu();
        ppu();
        apu();
    }
}

bool start(char *filename) {
    rom = fopen(filename, "rb");
    if (!rom) {
        printf("Failed to open ROM!\n");
        return false;
    }

    // Read the ROM header
    uint8_t header[0x10];
    fread(header, 1, 0x10, rom);
    mirror_type = (header[6] & 0x08) ? 4 : 2 + !(header[6] & 0x01);
    mapper_type = header[7] | (header[6] >> 4);

    // Verify the file format
    char filetype[4];
    memcpy(filetype, header, 3);
    filetype[3] = '\0';
    if (strcmp(filetype, "NES") != 0 || header[3] != 0x1A) {
        printf("Invalid ROM format!\n");
        return false;
    }

    // Load a savefile if the ROM supports it
    if (header[6] & 0x02) {
        char *ext = (char*)"sav";
        memcpy(&filename[strlen(filename) - 3], ext, 3);
        save = fopen(filename, "rb");
        if (save)
            fread(&memory[0x6000], 1, 0x2000, save);
        save = fopen(filename, "wb");
    }

    // Load the ROM trainer into memory
    if (header[6] & 0x04)
        fread(&memory[0x7000], 1, 0x200, rom);

    rom_address = ftell(rom);

    // Load the ROM banks into memory
    switch (mapper_type) {
        // Mirror a single 16 KB bank or load both 16 KB banks
        case 0: // NROM
        case 3: // CNROM
            fread(&memory[0x8000], 1, 0x4000, rom);
            if (header[4] == 1)
                memcpy(&memory[0xC000], &memory[0x8000], 0x4000);
            else
                fread(&memory[0xC000], 1, 0x4000, rom);
            break;

        // Load the first and last 16 KB banks
        case 1: // MMC1
        case 2: // UNROM
        case 4: // MMC3
            fread(&memory[0x8000], 1, 0x4000, rom);
            fseek(rom, (header[4] - 2) * 0x4000, SEEK_CUR);
            rom_address_last = ftell(rom);
            fread(&memory[0xC000], 1, 0x4000, rom);
            mapper_register = 0x04;
            break;

        default:
            printf("Unknown mapper type: %d\n", mapper_type);
            fclose(rom);
            return false;
    }

    // Load the first 8 KB of VROM into PPU memory
    if (header[5]) {
        vrom_address = ftell(rom);
        fread(ppu_memory, 1, 0x2000, rom);
    }

    // Reset the program counter
    interrupts[1] = true;

    return true;
}

void stop() {
    // Write the savefile
    if (save) {
        fwrite(&memory[0x6000], 1, 0x2000, save);
        fclose(save);
    }

    fclose(rom);
}

int16_t audio_mixer() {
    int16_t out = 0;

    // Generate the pulse waves
    for (int i = 0; i < 2; i++) {
        wavelengths[i] += 2;

        if ((duty_cycles[i] == 0 && wavelengths[i] <  pulses[i] / 8) ||
            (duty_cycles[i] == 1 && wavelengths[i] <  pulses[i] / 4) ||
            (duty_cycles[i] == 2 && wavelengths[i] <  pulses[i] / 2) ||
            (duty_cycles[i] == 3 && wavelengths[i] >= pulses[i] / 4))
            out += 0x200 * volumes[i];

            if (wavelengths[i] >= pulses[i])
                wavelengths[i] = 0;
    }

    return out;
}

void draw() {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 240, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, display);
    glBegin(GL_QUADS);
    glTexCoord2i(1, 1); glVertex2f( 1, -1);
    glTexCoord2i(0, 1); glVertex2f(-1, -1);
    glTexCoord2i(0, 0); glVertex2f(-1,  1);
    glTexCoord2i(1, 0); glVertex2f( 1,  1);
    glEnd();
    glFlush();
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

int audio_callback(const void *in, void *out, unsigned long frames,
                   const PaStreamCallbackTimeInfo *info, PaStreamCallbackFlags flags, void *data) {
    int16_t *out_cur = (int16_t*)out;
    for (int i = 0; i < frames; i++)
        *out_cur++ = audio_mixer();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify a ROM to load.\n");
        return 1;
    }

    if (!start(argv[1]))
        return 1;

    glutInit(&argc, argv);
    glutInitWindowSize(256, 240);
    glutCreateWindow("NoiES");
    glEnable(GL_TEXTURE_2D);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    PaStream *stream;
    Pa_Initialize();
    Pa_OpenDefaultStream(&stream, 0, 1, paInt16, 44100, 256, audio_callback, NULL);
    Pa_StartStream(stream);

    atexit(stop);
    glutDisplayFunc(draw);
    glutKeyboardFunc(key_down);
    glutKeyboardUpFunc(key_up);

    std::thread core(run);
    glutMainLoop();

    return 0;
}
