#include <chrono>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

uint8_t cpuMemory[0x10000];
uint8_t ppuMemory[0x4000];
uint8_t sprMemory[0x100];

uint32_t cycles, targetCycles;
uint16_t programCounter;
uint8_t accumulator, registerX, registerY;
uint8_t flags = 0x24; // NV_BDIZC
uint8_t stackPointer = 0xFF;
bool interrupts[3]; // NMI, RST, BRK

uint32_t ppuCycles;
uint16_t scanline;
uint8_t scrollX, scrollY;
uint8_t spriteCount;
uint8_t mirrorMode;
uint8_t ppuBuffer;
uint8_t ppuLatch;
bool ppuLatchOn;

uint8_t inputShift;

uint16_t apuTimers[2];
uint16_t pulses[2];
uint8_t dutyCycles[2];
uint8_t volumes[2];
int16_t wavelengths[2];

uint8_t mapperType;
uint8_t mapperRegister, mapperLatch, mapperShift;
uint8_t irqCounter, irqLatch;
bool irqEnable, irqReload;

FILE *rom, *save;
uint32_t romAddress, romAddressLast, vromAddress;

uint32_t framebuffer[256 * 240];
uint32_t displayBuffer[256 * 240];
std::chrono::steady_clock::time_point timer;

const uint32_t palette[] =
{
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
uint8_t *zeroPage()
{
    return &cpuMemory[cpuMemory[++programCounter]];
}

// Zero page X addressing: Add the X register to the zero page address
uint8_t *zeroPageX()
{
    return &cpuMemory[(cpuMemory[++programCounter] + registerX) % 0x0100];
}

// Zero page Y addressing: Add the Y register to the zero page address
uint8_t *zeroPageY()
{
    return &cpuMemory[(cpuMemory[++programCounter] + registerY) % 0x0100];
}

// Absolute addressing: Use the immediate 2 values as a memory address
uint8_t *absolute()
{
    return &cpuMemory[cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8)];
}

// Absolute X addressing: Add the X register to the absolute address
uint8_t *absoluteX(bool pageCycle)
{
    uint16_t address = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    if (pageCycle && address / 0x100 != (address + registerX) / 0x100) // Page cross
        targetCycles += 3;
    return &cpuMemory[address + registerX];
}

// Absolute Y addressing: Add the Y register to the absolute address
uint8_t *absoluteY(bool pageCycle)
{
    uint16_t address = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    if (pageCycle && address / 0x100 != (address + registerY) / 0x100) // Page cross
        targetCycles += 3;
    return &cpuMemory[address + registerY];
}

// Indirect addressing: Use the memory address stored at the absolute address
uint8_t *indirect()
{
    uint16_t address = cpuMemory[++programCounter] | (cpuMemory[++programCounter] << 8);
    uint16_t address2 = (((address + 1) & 0x00FF) == 0x00) ? address + 1 - 0x100 : address + 1;
    return &cpuMemory[cpuMemory[address] | (cpuMemory[address2] << 8)];
}

// Indirect X addressing: Use the memory address stored at the zero page X address 
uint8_t *indirectX()
{
    uint8_t addressLower = cpuMemory[(cpuMemory[++programCounter] + registerX) % 0x0100];
    uint8_t addressUpper = cpuMemory[(cpuMemory[programCounter] + registerX + 1) % 0x0100];
    return &cpuMemory[addressLower | (addressUpper << 8)];
}

// Indirect Y addressing: Add the Y register to the memory address stored at the zero page address
uint8_t *indirectY(bool pageCycle)
{
    uint8_t addressLower = cpuMemory[cpuMemory[++programCounter]];
    uint8_t addressUpper = cpuMemory[(cpuMemory[programCounter] + 1) % 0x100];
    uint16_t address = addressLower | (addressUpper << 8);
    if (pageCycle && address / 0x100 != (address + registerY) / 0x100) // Page cross
        targetCycles += 3;
    return &cpuMemory[address + registerY];
}

// Immediate addressing: Get the value immediately after the current address
uint8_t *immediate()
{
    return &cpuMemory[++programCounter];
}

// Get the real location of a mirrored address in memory
uint16_t cpuMemoryMirror(uint16_t address)
{
    if (address >= 0x0800 && address < 0x2000)
        address = address % 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + (address - 0x2000) % 8;
    return address;
}

// Get the real location of a mirrored address in PPU memory
uint16_t ppuMemoryMirror(uint16_t address)
{
    address %= 0x4000;
    if (address >= 0x3000 && address < 0x3F00)
        address -= 0x1000;
    else if (address >= 0x3F20 && address < 0x4000)
        address = 0x3F00 + (address - 0x3F20) % 20;
    else if (address >= 0x3F10 && address < 0x3F20 && address % 4 == 0)
        address -= 0x10;

    // Nametable mirroring
    switch (mirrorMode)
    {
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
void mapperWrite(uint16_t address, uint8_t value)
{
    switch (mapperType)
    {
        case 1: // MMC1: Swap 16 KB or 32 KB ROM banks and 4 KB or 8 KB VROM banks
            if (value & 0x80)
            {
                // Reset the shift register on a write with bit 7 set
                mapperLatch |= 0x0C;
                mapperShift = 0;
            }
            else
            {
                // Write one bit to the latch
                mapperLatch |= (value & 0x01) << mapperShift;
                mapperShift++;
            }

            if (mapperShift == 5)
            {
                if (address >= 0x8000 && address < 0xA000) // Control
                {
                    mapperRegister = mapperLatch;
                    mirrorMode = mapperLatch & 0x03;
                }
                else if (address >= 0xA000 && address < 0xC000) // VROM bank 0
                {
                    if (mapperRegister & 0x10) // 4 KB
                    {
                        fseek(rom, vromAddress + 0x1000 * mapperLatch, SEEK_SET);
                        fread(ppuMemory, 1, 0x1000, rom);
                    }
                    else // 8 KB
                    {
                        fseek(rom, vromAddress + 0x1000 * (mapperLatch & ~0x01), SEEK_SET);
                        fread(ppuMemory, 1, 0x2000, rom);
                    }
                }
                else if (address >= 0xC000 && address < 0xE000) // VROM bank 1
                {
                    if (mapperRegister & 0x10) // 4 KB
                    {
                        fseek(rom, vromAddress + 0x1000 * mapperLatch, SEEK_SET);
                        fread(&ppuMemory[0x1000], 1, 0x1000, rom);
                    }
                }
                else // ROM banks
                {
                    if (mapperRegister & 0x04) // ROM bank 1 is fixed
                    {
                        if (mapperRegister & 0x08) // 16 KB
                        {
                            fseek(rom, romAddress + 0x4000 * mapperLatch, SEEK_SET);
                            fread(&cpuMemory[0x8000], 1, 0x4000, rom);
                        }
                        else // 32 KB
                        {
                            fseek(rom, romAddress + 0x4000 * (mapperLatch & 0x0E), SEEK_SET);
                            fread(&cpuMemory[0x8000], 1, 0x8000, rom);
                        }

                        fseek(rom, romAddressLast, SEEK_SET);
                        fread(&cpuMemory[0xC000], 1, 0x4000, rom);
                    }
                    else // ROM bank 0 is fixed
                    {
                        if (mapperRegister & 0x08) // 16 KB
                        {
                            fseek(rom, romAddress + 0x4000 * mapperLatch, SEEK_SET);
                            fread(&cpuMemory[0xC000], 1, 0x4000, rom);
                        }
                        else // 32 KB
                        {
                            fseek(rom, romAddress + 0x4000 * (mapperLatch & 0x0E), SEEK_SET);
                            fread(&cpuMemory[0x8000], 1, 0x8000, rom);
                        }

                        fseek(rom, romAddress, SEEK_SET);
                        fread(&cpuMemory[0x8000], 1, 0x4000, rom);
                    }
                }

                mapperLatch = 0;
                mapperShift = 0;
            }

            break;

        case 2: // UNROM: Swap the first ROM bank only
            fseek(rom, romAddress + 0x4000 * value, SEEK_SET);
            fread(&cpuMemory[0x8000], 1, 0x4000, rom);
            break;

        case 3: // CNROM: Swap the VROM bank only
            fseek(rom, vromAddress + 0x2000 * (value & 0x03), SEEK_SET);
            fread(ppuMemory, 1, 0x2000, rom);
            break;

        case 4: // MMC3: Swap 8 KB ROM banks and 1 KB or 2 KB VROM banks
            if (address >= 0x8000 && address < 0xA000)
            {
                if (address % 2 == 0) // Bank select
                {
                    mapperRegister = value;
                    fseek(rom, romAddressLast, SEEK_SET);
                    fread(&cpuMemory[(value & 0x40) ? 0x8000 : 0xC000], 1, 0x2000, rom);
                }
                else // Bank data
                {
                    uint8_t bank = mapperRegister & 0x07;
                    if (bank < 2) // 2 KB VROM banks
                    {
                        fseek(rom, vromAddress + 0x0400 * (value & ~0x01), SEEK_SET);
                        fread(&ppuMemory[((mapperRegister & 0x80) << 5) + 0x0800 * bank], 1, 0x0800, rom);
                    }
                    else if (bank >= 2 && bank < 6) // 1 KB VROM banks
                    {
                        fseek(rom, vromAddress + 0x0400 * value, SEEK_SET);
                        fread(&ppuMemory[(!(mapperRegister & 0x80) << 12) + 0x0400 * (bank - 2)], 1, 0x0400, rom);
                    }
                    else if (bank == 6) // Swappable/fixed 8 KB ROM bank
                    {
                        fseek(rom, romAddress + 0x2000 * value, SEEK_SET);
                        fread(&cpuMemory[(mapperRegister & 0x40) ? 0xC000 : 0x8000], 1, 0x2000, rom);
                    }
                    else // Swappable 8 KB ROM bank
                    {
                        fseek(rom, romAddress + 0x2000 * value, SEEK_SET);
                        fread(&cpuMemory[0xA000], 1, 0x2000, rom);
                    }
                }
            }
            else if (address >= 0xA000 && address < 0xC000)
            {
                if (address % 2 == 0) // Mirroring
                {
                    if (mirrorMode != 4)
                        mirrorMode = 2 + value;
                }
            }
            else if (address >= 0xC000 && address < 0xE000)
            {
                if (address % 2 == 0) // IRQ latch
                {
                    irqLatch = value;
                }
                else // IRQ reload
                {
                    irqCounter = 0;
                    irqReload = true;
                }
            }
            else // IRQ toggle
            {
                irqEnable = (address % 2 == 1);
            }
            break;
    }
}

// CL_: Clear a flag
void cl_(uint8_t flag)
{
    flags &= ~flag;
}

// SE_: Set a flag
void se_(uint8_t flag)
{
    flags |= flag;
}

// PH_: Push a value to the stack
void ph_(uint8_t value)
{
    cpuMemory[0x0100 + stackPointer--] = value;
}

// PLA: Pull the accumulator from the stack
void pla()
{
    accumulator = cpuMemory[0x0100 + ++stackPointer];

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// PLP: Pull the flags from the stack
void plp()
{
    flags = cpuMemory[0x0100 + ++stackPointer];

    se_(0x20);
    cl_(0x10);
}

// ADC: Add with carry
void adc(uint8_t value)
{
    uint8_t oldAccum = accumulator;
    accumulator += value + (flags & 0x01);

    if (accumulator & 0x80)                                                      se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                        se_(0x02); else cl_(0x02); // Z
    if ((value & 0x80) == (oldAccum & 0x80) && (flags & 0x80) != (value & 0x80)) se_(0x40); else cl_(0x40); // V
    if (oldAccum > accumulator || value + (flags & 0x01) == 0x100)               se_(0x01); else cl_(0x01); // C
}

// AND: Bitwise and
void _and(uint8_t value)
{
    accumulator &= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// ASL: Arithmetic shift left
void asl(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value <<= 1;

    if (*value & 0x80)    se_(0x80); else cl_(0x80); // N
    if (*value == 0)      se_(0x02); else cl_(0x02); // Z
    if (oldValue & 0x80) se_(0x01);  else cl_(0x01); // C
}

// BIT: Test bits
void bit(uint8_t value)
{
    if (value & 0x80)               se_(0x80); else cl_(0x80); // N
    if (value & 0x40)               se_(0x40); else cl_(0x40); // V
    if ((accumulator & value) == 0) se_(0x02); else cl_(0x02); // Z
}

// B__: Branch on condition
void b__(bool condition)
{
    int8_t value = *immediate();
    if (condition)
    {
        programCounter += value;
        targetCycles += 3;
        if ((programCounter + 1) / 0x100 != (programCounter - value) / 0x100) // Page cross
            targetCycles += 3;
    }
}

// BRK: Break
void brk()
{
    if (!(flags & 0x04))
    {
        se_(0x10);
        interrupts[2] = true;
    }
    programCounter++;
}

// CP_: Compare a register
void cp_(uint8_t reg, uint8_t value)
{
    if ((reg - value) & 0x80) se_(0x80); else cl_(0x80); // N
    if (reg == value)         se_(0x02); else cl_(0x02); // Z
    if (reg >= value)         se_(0x01); else cl_(0x01); // C
}

// DE_: Decrement a value
void de_(uint8_t *value)
{
    (*value)--;

    if (*value & 0x80) se_(0x80); else cl_(0x80); // N
    if (*value == 0)   se_(0x02); else cl_(0x02); // Z
}

// EOR: Bitwise exclusive or
void eor(uint8_t value)
{
    accumulator ^= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// IN_: Increment a value
void in_(uint8_t *value)
{
    (*value)++;

    if (*value & 0x80) se_(0x80); else cl_(0x80); // N
    if (*value == 0)   se_(0x02); else cl_(0x02); // Z
}

// JMP: Jump
void jmp(uint8_t *address)
{
    programCounter = address - cpuMemory - 1;
}

// JSR: Jump to subroutine
void jsr(uint8_t *address)
{
    ph_(programCounter >> 8);
    ph_(programCounter);
    programCounter = address - cpuMemory - 1;
}

// LD_: Load a register
void ld_(uint8_t *reg, uint8_t *value)
{
    uint16_t address = cpuMemoryMirror(value - cpuMemory);
    *reg = cpuMemory[address];

    if (*reg & 0x80) se_(0x80); else cl_(0x80); // N
    if (*reg == 0)   se_(0x02); else cl_(0x02); // Z

    // Handle I/O and PPU registers
    switch (address)
    {
        case 0x4016: // JOYPAD1: Read button status 1 bit at a time
            *reg = (cpuMemory[0x4016] & (1 << inputShift)) ? 0x41 : 0x40;
            inputShift++;
            if (inputShift == 8)
                inputShift = 0;
            break;

        case 0x2002: // PPUSTATUS: Clear V-blank bit and latch
            cpuMemory[0x2002] &= ~0x80;
            ppuLatchOn = false;
            break;

        case 0x2004: // OAMDATA: Read from sprite memory
            *reg = sprMemory[cpuMemory[0x2003]];
            break;

        case 0x2007: // PPUDATA: Read from PPU memory
            uint16_t ppuAddress = (ppuLatch << 8) | cpuMemory[0x2006];

            // Buffer non-palette reads
            if (ppuAddress < 0x3F00)
                *reg = ppuBuffer;
            else
                *reg = ppuMemory[ppuMemoryMirror(ppuAddress)];

            ppuBuffer = ppuMemory[ppuMemoryMirror(ppuAddress)];

            // Increment the address
            ppuAddress += (cpuMemory[0x2000] & 0x04) ? 32 : 1;
            ppuLatch = ppuAddress >> 8;
            cpuMemory[0x2006] = ppuAddress;

            break;
    }
}

// LSR: Logical shift right
void lsr(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value = (*value >> 1) & ~0x80;

    if (*value & 0x80)   se_(0x80); else cl_(0x80); // N
    if (*value == 0)     se_(0x02); else cl_(0x02); // Z
    if (oldValue & 0x01) se_(0x01); else cl_(0x01); // C
}

// ORA: Bitwise or
void ora(uint8_t value)
{
    accumulator |= value;

    if (accumulator & 0x80) se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)   se_(0x02); else cl_(0x02); // Z
}

// T__: Transfer one register to another
void t__(uint8_t *src, uint8_t *dst)
{
    *dst = *src;

    if (*dst & 0x80) se_(0x80); else cl_(0x80); // N
    if (*dst == 0)   se_(0x02); else cl_(0x02); // Z
}

// TXS: Transfer the X register to the stack pointer
void txs()
{
    stackPointer = registerX;
}

// ROL: Rotate left
void rol(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value = (*value << 1) | (flags & 0x01);

    if (*value & 0x80)   se_(0x80); else cl_(0x80); // N
    if (*value == 0)     se_(0x02); else cl_(0x02); // Z
    if (oldValue & 0x80) se_(0x01); else cl_(0x01); // C
}

// ROR: Rotate right
void ror(uint8_t *value)
{
    uint8_t oldValue = *value;
    *value = ((*value >> 1) & ~0x80) | ((flags & 0x01) << 7);

    if (*value & 0x80)   se_(0x80); else cl_(0x80); // N
    if (*value == 0)     se_(0x02); else cl_(0x02); // Z
    if (oldValue & 0x01) se_(0x01); else cl_(0x01); // C
}

// RTS: Return from subroutine
void rts()
{
    programCounter = cpuMemory[0x0100 + ++stackPointer] | (cpuMemory[0x0100 + ++stackPointer] << 8);
}

// RTI: Return from interrupt
void rti()
{
    plp();
    rts();
    programCounter--;
}

// SBC: Subtract with carry
void sbc(uint8_t value)
{
    uint8_t oldAccum = accumulator;
    accumulator -= value + !(flags & 0x01);

    if (accumulator & 0x80)                                                      se_(0x80); else cl_(0x80); // N
    if (accumulator == 0)                                                        se_(0x02); else cl_(0x02); // Z
    if ((value & 0x80) != (oldAccum & 0x80) && (flags & 0x80) == (value & 0x80)) se_(0x40); else cl_(0x40); // V
    if (oldAccum >= accumulator && value + !(flags & 0x01) != 0x100)             se_(0x01); else cl_(0x01); // C
}

// ST_: Store a register
void st_(uint8_t reg, uint8_t *dst)
{
    uint16_t address = cpuMemoryMirror(dst - cpuMemory);

    if (address >= 0x8000)
        mapperWrite(address, reg);
    else if (address != 0x2002 && address != 0x4016)
        cpuMemory[address] = reg;

    // Handle PPU registers
    switch (address)
    {
        case 0x4014: // OAMDMA: DMA transfer to sprite memory
            memcpy(sprMemory, &cpuMemory[cpuMemory[0x4014] * 0x100], 0x100);
            targetCycles += ((targetCycles / 3) % 2) ? 1542 : 1539;
            break;

        case 0x2004: // OAMDATA: 1 byte transfer to sprite memory
            sprMemory[cpuMemory[0x2003]] = reg;
            cpuMemory[0x2003]++;
            break;

        case 0x2005: // PPUSCROLL: Second write sets the scroll positions
            ppuLatchOn = !ppuLatchOn;
            if (ppuLatchOn)
            {
                ppuLatch = reg;
            }
            else
            {
                scrollX = ppuLatch;
                scrollY = reg;
            }
            break;

        case 0x2006: // PPUADDR: First write goes to the latch
            ppuLatchOn = !ppuLatchOn;
            if (ppuLatchOn)
                ppuLatch = reg;
            break;

        case 0x2007: // PPUDATA: 1 byte transfer to PPU memory
            uint16_t ppuAddress = (ppuLatch << 8) | cpuMemory[0x2006];

            ppuMemory[ppuMemoryMirror(ppuAddress)] = reg;

            // Increment the address
            ppuAddress += (cpuMemory[0x2000] & 0x04) ? 32 : 1;
            ppuLatch = ppuAddress >> 8;
            cpuMemory[0x2006] = ppuAddress;

            break;
    }
}

void cpu()
{
    cycles++;
    if (cycles < targetCycles)
        return;

    // Reset the cycle counter every second
    if (targetCycles > 5360520)
    {
        targetCycles -= 5360520;
        cycles -= 5360520;
    }

    // Handle interrupts
    for (int i = 0; i < 3; i++)
    {
        if (interrupts[i])
        {
            ph_(programCounter >> 8);
            ph_(programCounter);
            ph_(flags);
            cl_(0x10);
            se_(0x04);
            programCounter = cpuMemory[0xFFFB + i * 2] << 8 | cpuMemory[0xFFFA + i * 2];
            targetCycles += 21;
            interrupts[i] = false;
        }
    }

    // Decode opcode
    switch (cpuMemory[programCounter])
    {
        case 0x69: adc(*immediate());     targetCycles += 6;  break; // ADC immediate
        case 0x65: adc(*zeroPage());      targetCycles += 9;  break; // ADC zero page
        case 0x75: adc(*zeroPageX());     targetCycles += 12; break; // ADC zero page X
        case 0x6D: adc(*absolute());      targetCycles += 12; break; // ADC absolute
        case 0x7D: adc(*absoluteX(true)); targetCycles += 12; break; // ADC absolute X
        case 0x79: adc(*absoluteY(true)); targetCycles += 12; break; // ADC absolute Y
        case 0x61: adc(*indirectX());     targetCycles += 18; break; // ADC indirect X
        case 0x71: adc(*indirectY(true)); targetCycles += 15; break; // ADC indirect Y

        case 0x29: _and(*immediate());     targetCycles += 6;  break; // AND immediate
        case 0x25: _and(*zeroPage());      targetCycles += 9;  break; // AND zero page
        case 0x35: _and(*zeroPageX());     targetCycles += 12; break; // AND zero page X
        case 0x2D: _and(*absolute());      targetCycles += 12; break; // AND absolute
        case 0x3D: _and(*absoluteX(true)); targetCycles += 12; break; // AND absolute X
        case 0x39: _and(*absoluteY(true)); targetCycles += 12; break; // AND absolute Y
        case 0x21: _and(*indirectX());     targetCycles += 18; break; // AND indirect X
        case 0x31: _and(*indirectY(true)); targetCycles += 15; break; // AND indirect Y

        case 0x0A: asl(&accumulator);     targetCycles += 6;  break; // ASL accumulator
        case 0x06: asl(zeroPage());       targetCycles += 15; break; // ASL zero page
        case 0x16: asl(zeroPageX());      targetCycles += 18; break; // ASL zero page X
        case 0x0E: asl(absolute());       targetCycles += 18; break; // ASL absolute
        case 0x1E: asl(absoluteX(false)); targetCycles += 21; break; // ASL absolute X

        case 0x24: bit(*zeroPage()); targetCycles += 9;  break; // BIT zero page
        case 0x2C: bit(*absolute()); targetCycles += 12; break; // BIT absolute

        case 0x10: b__(!(flags & 0x80)); targetCycles += 6; break; // BPL
        case 0x30: b__( (flags & 0x80)); targetCycles += 6; break; // BMI
        case 0x50: b__(!(flags & 0x40)); targetCycles += 6; break; // BVC
        case 0x70: b__( (flags & 0x40)); targetCycles += 6; break; // BVS
        case 0x90: b__(!(flags & 0x01)); targetCycles += 6; break; // BCC
        case 0xB0: b__( (flags & 0x01)); targetCycles += 6; break; // BCS
        case 0xD0: b__(!(flags & 0x02)); targetCycles += 6; break; // BNE
        case 0xF0: b__( (flags & 0x02)); targetCycles += 6; break; // BEQ

        case 0x00: brk(); targetCycles += 21; break; // BRK

        case 0xC9: cp_(accumulator, *immediate());     targetCycles += 6;  break; // CMP immediate
        case 0xC5: cp_(accumulator, *zeroPage());      targetCycles += 9;  break; // CMP zero page
        case 0xD5: cp_(accumulator, *zeroPageX());     targetCycles += 12; break; // CMP zero page X
        case 0xCD: cp_(accumulator, *absolute());      targetCycles += 12; break; // CMP absolute
        case 0xDD: cp_(accumulator, *absoluteX(true)); targetCycles += 12; break; // CMP absolute X
        case 0xD9: cp_(accumulator, *absoluteY(true)); targetCycles += 12; break; // CMP absolute Y
        case 0xC1: cp_(accumulator, *indirectX());     targetCycles += 18; break; // CMP indirect X
        case 0xD1: cp_(accumulator, *indirectY(true)); targetCycles += 15; break; // CMP indirect Y

        case 0xE0: cp_(registerX, *immediate()); targetCycles += 6;  break; // CPX immediate
        case 0xE4: cp_(registerX, *zeroPage());  targetCycles += 9;  break; // CPX zero page
        case 0xEC: cp_(registerX, *absolute());  targetCycles += 12; break; // CPX absolute

        case 0xC0: cp_(registerY, *immediate()); targetCycles += 6;  break; // CPY immediate
        case 0xC4: cp_(registerY, *zeroPage());  targetCycles += 9;  break; // CPY zero page
        case 0xCC: cp_(registerY, *absolute());  targetCycles += 12; break; // CPY absolute

        case 0xC6: de_(zeroPage());       targetCycles += 15; break; // DEC zero page
        case 0xD6: de_(zeroPageX());      targetCycles += 18; break; // DEC zero page X
        case 0xCE: de_(absolute());       targetCycles += 18; break; // DEC absolute
        case 0xDE: de_(absoluteX(false)); targetCycles += 21; break; // DEC absolute X
        
        case 0x49: eor(*immediate());     targetCycles += 6;  break; // EOR immediate
        case 0x45: eor(*zeroPage());      targetCycles += 9;  break; // EOR zero page
        case 0x55: eor(*zeroPageX());     targetCycles += 12; break; // EOR zero page X
        case 0x4D: eor(*absolute());      targetCycles += 12; break; // EOR absolute
        case 0x5D: eor(*absoluteX(true)); targetCycles += 12; break; // EOR absolute X
        case 0x59: eor(*absoluteY(true)); targetCycles += 12; break; // EOR absolute Y
        case 0x41: eor(*indirectX());     targetCycles += 18; break; // EOR indirect X
        case 0x51: eor(*indirectY(true)); targetCycles += 15; break; // EOR indirect Y

        case 0x18: cl_(0x01); targetCycles += 6; break; // CLC
        case 0x38: se_(0x01); targetCycles += 6; break; // SEC
        case 0x58: cl_(0x04); targetCycles += 6; break; // CLI
        case 0x78: se_(0x04); targetCycles += 6; break; // SEI
        case 0xB8: cl_(0x40); targetCycles += 6; break; // CLV
        case 0xD8: cl_(0x08); targetCycles += 6; break; // CLD
        case 0xF8: se_(0x08); targetCycles += 6; break; // SED

        case 0xE6: in_(zeroPage());       targetCycles += 15; break; // INC zero page
        case 0xF6: in_(zeroPageX());      targetCycles += 18; break; // INC zero page X
        case 0xEE: in_(absolute());       targetCycles += 18; break; // INC absolute
        case 0xFE: in_(absoluteX(false)); targetCycles += 21; break; // INC absolute X

        case 0x4C: jmp(absolute()); targetCycles += 9;  break; // JMP absolute
        case 0x6C: jmp(indirect()); targetCycles += 15; break; // JMP indirect

        case 0x20: jsr(absolute()); targetCycles += 18; break; // JSR absolute

        case 0xA9: ld_(&accumulator, immediate());     targetCycles += 6;  break; // LDA immediate
        case 0xA5: ld_(&accumulator, zeroPage());      targetCycles += 9;  break; // LDA zero page
        case 0xB5: ld_(&accumulator, zeroPageX());     targetCycles += 12; break; // LDA zero page X
        case 0xAD: ld_(&accumulator, absolute());      targetCycles += 12; break; // LDA absolute
        case 0xBD: ld_(&accumulator, absoluteX(true)); targetCycles += 12; break; // LDA absolute X
        case 0xB9: ld_(&accumulator, absoluteY(true)); targetCycles += 12; break; // LDA absolute Y
        case 0xA1: ld_(&accumulator, indirectX());     targetCycles += 18; break; // LDA indirect X
        case 0xB1: ld_(&accumulator, indirectY(true)); targetCycles += 15; break; // LDA indirect Y

        case 0xA2: ld_(&registerX, immediate());     targetCycles += 6; break; // LDX immediate
        case 0xA6: ld_(&registerX, zeroPage());      targetCycles += 6; break; // LDX zero page
        case 0xB6: ld_(&registerX, zeroPageY());     targetCycles += 6; break; // LDX zero page Y
        case 0xAE: ld_(&registerX, absolute());      targetCycles += 9; break; // LDX absolute
        case 0xBE: ld_(&registerX, absoluteY(true)); targetCycles += 9; break; // LDX absolute Y

        case 0xA0: ld_(&registerY, immediate());     targetCycles += 6; break; // LDY immediate
        case 0xA4: ld_(&registerY, zeroPage());      targetCycles += 6; break; // LDY zero page
        case 0xB4: ld_(&registerY, zeroPageX());     targetCycles += 6; break; // LDY zero page X
        case 0xAC: ld_(&registerY, absolute());      targetCycles += 9; break; // LDY absolute
        case 0xBC: ld_(&registerY, absoluteX(true)); targetCycles += 9; break; // LDY absolute X

        case 0x4A: lsr(&accumulator);     targetCycles += 6;  break; // LSR accumulator
        case 0x46: lsr(zeroPage());       targetCycles += 15; break; // LSR zero page
        case 0x56: lsr(zeroPageX());      targetCycles += 18; break; // LSR zero page X
        case 0x4E: lsr(absolute());       targetCycles += 18; break; // LSR absolute
        case 0x5E: lsr(absoluteX(false)); targetCycles += 21; break; // LSR absolute X

        case 0xEA: targetCycles += 6; break; // NOP

        case 0x09: ora(*immediate());     targetCycles += 6;  break; // ORA immediate
        case 0x05: ora(*zeroPage());      targetCycles += 9;  break; // ORA zero page
        case 0x15: ora(*zeroPageX());     targetCycles += 12; break; // ORA zero page X
        case 0x0D: ora(*absolute());      targetCycles += 12; break; // ORA absolute
        case 0x1D: ora(*absoluteX(true)); targetCycles += 12; break; // ORA absolute X
        case 0x19: ora(*absoluteY(true)); targetCycles += 12; break; // ORA absolute Y
        case 0x01: ora(*indirectX());     targetCycles += 18; break; // ORA indirect X
        case 0x11: ora(*indirectY(true)); targetCycles += 15; break; // ORA indirect Y

        case 0xAA: t__(&accumulator, &registerX); targetCycles += 6; break; // TAX
        case 0x8A: t__(&registerX, &accumulator); targetCycles += 6; break; // TXA
        case 0xCA: de_(&registerX);               targetCycles += 6; break; // DEX
        case 0xE8: in_(&registerX);               targetCycles += 6; break; // INX
        case 0xA8: t__(&accumulator, &registerY); targetCycles += 6; break; // TAY
        case 0x98: t__(&registerY, &accumulator); targetCycles += 6; break; // TYA
        case 0x88: de_(&registerY);               targetCycles += 6; break; // DEY
        case 0xC8: in_(&registerY);               targetCycles += 6; break; // INY

        case 0x2A: rol(&accumulator);     targetCycles += 6;  break; // ROL accumulator
        case 0x26: rol(zeroPage());       targetCycles += 15; break; // ROL zero page
        case 0x36: rol(zeroPageX());      targetCycles += 18; break; // ROL zero page X
        case 0x2E: rol(absolute());       targetCycles += 18; break; // ROL absolute
        case 0x3E: rol(absoluteX(false)); targetCycles += 21; break; // ROL absolute X

        case 0x6A: ror(&accumulator);     targetCycles += 6;  break; // ROR accumulator
        case 0x66: ror(zeroPage());       targetCycles += 15; break; // ROR zero page
        case 0x76: ror(zeroPageX());      targetCycles += 18; break; // ROR zero page X
        case 0x6E: ror(absolute());       targetCycles += 18; break; // ROR absolute
        case 0x7E: ror(absoluteX(false)); targetCycles += 21; break; // ROR absolute X

        case 0x40: rti(); targetCycles += 18; break; // RTI

        case 0x60: rts(); targetCycles += 18; break; // RTS

        case 0xE9: sbc(*immediate());     targetCycles += 6;  break; // SBC immediate
        case 0xE5: sbc(*zeroPage());      targetCycles += 9;  break; // SBC zero page
        case 0xF5: sbc(*zeroPageX());     targetCycles += 12; break; // SBC zero page X
        case 0xED: sbc(*absolute());      targetCycles += 12; break; // SBC absolute
        case 0xFD: sbc(*absoluteX(true)); targetCycles += 12; break; // SBC absolute X
        case 0xF9: sbc(*absoluteY(true)); targetCycles += 12; break; // SBC absolute Y
        case 0xE1: sbc(*indirectX());     targetCycles += 18; break; // SBC indirect X
        case 0xF1: sbc(*indirectY(true)); targetCycles += 15; break; // SBC indirect Y

        case 0x85: st_(accumulator, zeroPage());       targetCycles += 9;  break; // STA zero page
        case 0x95: st_(accumulator, zeroPageX());      targetCycles += 12; break; // STA zero page X
        case 0x8D: st_(accumulator, absolute());       targetCycles += 12; break; // STA absolute
        case 0x9D: st_(accumulator, absoluteX(false)); targetCycles += 15; break; // STA absolute X
        case 0x99: st_(accumulator, absoluteY(false)); targetCycles += 15; break; // STA absolute Y
        case 0x81: st_(accumulator, indirectX());      targetCycles += 18; break; // STA indirect X
        case 0x91: st_(accumulator, indirectY(false)); targetCycles += 18; break; // STA indirect Y

        case 0x9A: txs();                          targetCycles += 6;  break; // TXS
        case 0xBA: t__(&stackPointer, &registerX); targetCycles += 6;  break; // TSX
        case 0x48: ph_(accumulator);               targetCycles += 9;  break; // PHA
        case 0x68: pla();                          targetCycles += 12; break; // PLA
        case 0x08: ph_(flags | 0x10);              targetCycles += 9;  break; // PHP
        case 0x28: plp();                          targetCycles += 12; break; // PLP

        case 0x86: st_(registerX, zeroPage());  targetCycles += 9;  break; // STX zero page
        case 0x96: st_(registerX, zeroPageY()); targetCycles += 12; break; // STX zero page Y
        case 0x8E: st_(registerX, absolute());  targetCycles += 12; break; // STX absolute

        case 0x84: st_(registerY, zeroPage());  targetCycles += 9;  break; // STY zero page
        case 0x94: st_(registerY, zeroPageX()); targetCycles += 12; break; // STY zero page X
        case 0x8C: st_(registerY, absolute());  targetCycles += 12; break; // STY absolute

        default:
            printf("Unknown opcode: 0x%X\n", cpuMemory[programCounter]);
            break;
    }

    programCounter++;
}

void ppu()
{
    if (scanline >= 0 && scanline < 240) // Visible lines
    {
        if (ppuCycles >= 1 && ppuCycles <= 256 && cpuMemory[0x2001] & 0x08) // Background drawing
        {
            uint16_t x = ppuCycles - 1;
            uint16_t xOffset = x + scrollX;
            uint16_t yOffset = scanline + scrollY;
            uint16_t tableOffset = 0x2000 + (cpuMemory[0x2000] & 0x03) * 0x0400;
            uint16_t patternOffset = (cpuMemory[0x2000] & 0x10) << 8;

            // Change nametable based on the scroll offset
            if (xOffset >= 256)
            {
                tableOffset += 0x0400;
                xOffset %= 256;
            }
            if (yOffset >= 240)
            {
                tableOffset += 0x0800;
                yOffset %= 240;
            }
            tableOffset = ppuMemoryMirror(tableOffset);

            uint16_t tile = patternOffset + ppuMemory[tableOffset + (yOffset / 8) * 32 + xOffset / 8] * 16;
            uint8_t lowBits = ppuMemory[tile + yOffset % 8] & (0x80 >> (xOffset % 8)) ? 0x01 : 0x00;
            lowBits |= ppuMemory[tile + yOffset % 8 + 8] & (0x80 >> (xOffset % 8)) ? 0x02 : 0x00;

            // Get the upper 2 bits of the palette index from the attribute table
            uint8_t highBits = ppuMemory[tableOffset + 0x03C0 + (yOffset / 32) * 8 + xOffset / 32];
            if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 0) // Top left
                highBits = (highBits & 0x03) << 2;
            else if ((xOffset / 16) % 2 == 1 && (yOffset / 16) % 2 == 0) // Top right
                highBits = (highBits & 0x0C) << 0;
            else if ((xOffset / 16) % 2 == 0 && (yOffset / 16) % 2 == 1) // Bottom left
                highBits = (highBits & 0x30) >> 2;
            else // Bottom right
                highBits = (highBits & 0xC0) >> 4;

            if ((x >= 8 || cpuMemory[0x2001] & 0x02) && lowBits != 0)
            {
                uint8_t type = framebuffer[scanline * 256 + x];

                // Check for a sprite 0 hit
                if (type < 0xFD)
                    cpuMemory[0x2002] |= 0x40;

                // Draw a pixel
                if (type % 2 == 1)
                    framebuffer[scanline * 256 + x] = palette[ppuMemory[0x3F00 | highBits | lowBits]];
            }
        }
        else if (ppuCycles >= 257 && ppuCycles <= 320 && cpuMemory[0x2001] & 0x10) // Sprite drawing
        {
            uint8_t *sprite = &sprMemory[(ppuCycles - 257) * 4];
            uint8_t height = (cpuMemory[0x2000] & 0x20) ? 16 : 8;

            if (*sprite <= scanline && *sprite + height > scanline)
            {
                if (spriteCount < 8)
                {
                    uint8_t x = *(sprite + 3);
                    uint8_t spriteY = ((scanline - *sprite) / 8) * 16 + (scanline - *sprite) % 8;
                    uint16_t patternOffset = (height == 8) ? (cpuMemory[0x2000] & 0x08) << 9 : (*(sprite + 1) & 0x01) << 12;
                    uint16_t tile = patternOffset + (*(sprite + 1) & (height == 8 ? ~0x00 : ~0x01)) * 16;
                    uint8_t highBits = (*(sprite + 2) & 0x03) << 2;

                    if (*(sprite + 2) & 0x80)
                    {
                        if (height == 16 && spriteY < 8)
                            tile += 16;
                        spriteY = 7 - (spriteY % 8);
                    }

                    // Draw a sprite line on the next line
                    for (int i = 0; i < 8; i++)
                    {
                        uint16_t xOffset = x + ((*(sprite + 2) & 0x40) ? 7 - i : i);
                        uint8_t lowBits = ppuMemory[tile + spriteY] & (0x80 >> i) ? 0x01 : 0x00;
                        lowBits |= ppuMemory[tile + spriteY + 8] & (0x80 >> i) ? 0x02 : 0x00;

                        if (xOffset < 256 && (xOffset >= 8 || cpuMemory[0x2001] & 0x04) && lowBits != 0)
                        {
                            framebuffer[(scanline + 1) * 256 + xOffset] = palette[ppuMemory[0x3F10 | highBits | lowBits]];

                            // Mark opaque pixels
                            framebuffer[(scanline + 1) * 256 + xOffset]--;
                            if (*(sprite + 2) & 0x20) // Sprite is behind the background
                                framebuffer[(scanline + 1) * 256 + xOffset]--;
                            if (ppuCycles == 257) // Sprite 0
                                framebuffer[(scanline + 1) * 256 + xOffset] -= 2;
                        }
                    }

                    spriteCount++;
                }
                else // Sprite overflow
                {
                    cpuMemory[0x2002] |= 0x20;
                }
            }
        }

        // MMC3 IRQ counter
        if (mapperType == 4 && ppuCycles == 260 && cpuMemory[0x2001] & 0x08 && cpuMemory[0x2001] & 0x10)
        {
            if (irqCounter == 0)
            {
                // Trigger an IRQ if they're enabled
                if (irqEnable && !irqReload)
                    interrupts[2] = true;

                // Reload the counter
                irqCounter = irqLatch;
                irqReload = false;
            }
            else
            {
                irqCounter--;
            }
        }
    }
    else if (scanline == 241 && ppuCycles == 1) // Start of the V-blank period
    {
        cpuMemory[0x2002] |= 0x80;
        if (cpuMemory[0x2000] & 0x80)
            interrupts[0] = true;
    }
    else if (scanline == 261 && ppuCycles == 1) // Pre-render line
    {
        // Clear the bits for the next frame
        cpuMemory[0x2002] &= ~0xE0;
    }

    ppuCycles++;

    if (ppuCycles == 341) // End of a scanline
    {
        spriteCount = 0;
        ppuCycles = 0;
        scanline++;
    }

    if (scanline == 262) // End of a frame
    {
        scanline = 0;

        // Copy the finished frame to the display
        memcpy(displayBuffer, framebuffer, sizeof(displayBuffer));

        // Clear the framebuffer
        for (int i = 0; i < 256 * 240; i++)
            framebuffer[i] = palette[ppuMemory[0x3F00]];

        // Limit the FPS to 60
        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - timer;
        if (elapsed.count() < 1.0f / 60)
            usleep((1.0f / 60 - elapsed.count()) * 1000000);
        timer = std::chrono::steady_clock::now();
    }
}

void apu()
{
    if (cycles % 6 == 0) // Every other CPU cycle
    {
        // Decrement and reload the pulse channel timers
        for (int i = 0; i < 2; i++)
        {
            if (apuTimers[i] == 0)
            {
                apuTimers[i] = pulses[i] = ((cpuMemory[0x4003 + 4 * i] & 0x07) << 8) | cpuMemory[0x4002 + 4 * i];
                dutyCycles[i] = (cpuMemory[0x4004 + 4 * i] & 0xC0) >> 6;
                volumes[i] = (cpuMemory[0x4000 + 4 * i] & 0x10) ? (cpuMemory[0x4000 + 4 * i] & 0x0F) : 7;
            }
            else
            {
                apuTimers[i]--;
            }
        }
    }
}

bool loadRom(char *filename)
{
    rom = fopen(filename, "rb");
    if (!rom)
    {
        printf("Failed to open ROM!\n");
        return false;
    }

    // Read the ROM header
    uint8_t header[0x10];
    fread(header, 1, 0x10, rom);
    mirrorMode = (header[6] & 0x08) ? 4 : 2 + !(header[6] & 0x01);
    mapperType = header[7] | (header[6] >> 4);

    // Verify the file format
    char filetype[4];
    memcpy(filetype, header, 3);
    filetype[3] = '\0';
    if (strcmp(filetype, "NES") != 0 || header[3] != 0x1A)
    {
        printf("Invalid ROM format!\n");
        return false;
    }

    // Load a savefile if the ROM supports it
    if (header[6] & 0x02)
    {
        char *ext = (char*)"sav";
        memcpy(&filename[strlen(filename) - 3], ext, 3);
        save = fopen(filename, "rb");
        if (save)
            fread(&cpuMemory[0x6000], 1, 0x2000, save);
        save = fopen(filename, "wb");
    }

    // Load the ROM trainer into memory
    if (header[6] & 0x04)
        fread(&cpuMemory[0x7000], 1, 0x200, rom);

    romAddress = ftell(rom);

    // Load the ROM banks into memory
    switch (mapperType)
    {
        // Mirror a single 16 KB bank or load both 16 KB banks
        case 0: // NROM
        case 3: // CNROM
            fread(&cpuMemory[0x8000], 1, 0x4000, rom);
            if (header[4] == 1)
                memcpy(&cpuMemory[0xC000], &cpuMemory[0x8000], 0x4000);
            else
                fread(&cpuMemory[0xC000], 1, 0x4000, rom);
            break;

        // Load the first and last 16 KB banks
        case 1: // MMC1
        case 2: // UNROM
        case 4: // MMC3
            fread(&cpuMemory[0x8000], 1, 0x4000, rom);
            fseek(rom, (header[4] - 2) * 0x4000, SEEK_CUR);
            romAddressLast = ftell(rom);
            fread(&cpuMemory[0xC000], 1, 0x4000, rom);
            mapperRegister = 0x04;
            break;

        default:
            printf("Unknown mapper type: %d\n", mapperType);
            fclose(rom);
            return false;
    }

    // Load the first 8 KB of VROM into PPU memory
    if (header[5])
    {
        vromAddress = ftell(rom);
        fread(ppuMemory, 1, 0x2000, rom);
    }

    // Reset the program counter
    interrupts[1] = true;

    return true;
}

void closeRom()
{
    // Write the savefile
    if (save)
    {
        fwrite(&cpuMemory[0x6000], 1, 0x2000, save);
        fclose(save);
    }

    fclose(rom);
}

void runCycle()
{
    cpu();
    ppu();
    apu();
}

int16_t audioSample(uint8_t pitch)
{
    int16_t out = 0;

    // Generate the pulse waves
    for (int i = 0; i < 2; i++)
    {
        wavelengths[i] += pitch;

        if ((dutyCycles[i] == 0 && wavelengths[i] <  pulses[i] / 8) ||
            (dutyCycles[i] == 1 && wavelengths[i] <  pulses[i] / 4) ||
            (dutyCycles[i] == 2 && wavelengths[i] <  pulses[i] / 2) ||
            (dutyCycles[i] == 3 && wavelengths[i] >= pulses[i] / 4))
            out += 0x200 * volumes[i];

        if (wavelengths[i] >= pulses[i])
            wavelengths[i] = 0;
    }

    return out;
}

void pressKey(uint8_t key)
{
    cpuMemory[0x4016] |= 1 << key;
}

void releaseKey(uint8_t key)
{
    cpuMemory[0x4016] &= ~(1 << key);
}
