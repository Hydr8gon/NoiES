/*
    Copyright 2019 Hydr8gon

    This file is part of NoiES.

    NoiES is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NoiES is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NoiES. If not, see <https://www.gnu.org/licenses/>.
*/

#include <cstring>
#include <vector>

#include "core.h"
#include "ppu.h"
#include "apu.h"
#include "mapper.h"

namespace cpu
{

uint8_t memory[0x10000];

uint16_t cycles, targetCycles;
uint16_t programCounter;
uint8_t accumulator, registerX, registerY;
uint8_t flags; // NVBBDIZC
uint8_t stackPointer;
bool interrupts[3]; // NMI, RST, IRQ

uint8_t inputMask, inputShift;

const vector<core::StateItem> stateItems =
{
    { memory,          sizeof(memory)         },
    { &cycles,         sizeof(cycles)         },
    { &targetCycles,   sizeof(targetCycles)   },
    { &programCounter, sizeof(programCounter) },
    { &accumulator,    sizeof(accumulator)    },
    { &registerX,      sizeof(registerX)      },
    { &registerY,      sizeof(registerY)      },
    { &flags,          sizeof(flags)          },
    { &stackPointer,   sizeof(stackPointer)   },
    { interrupts,      sizeof(interrupts)     },
    { &inputShift,     sizeof(inputShift)     }
};

void reset()
{
    // Clear the state items
    for (unsigned int i = 0; i < stateItems.size(); i++)
        memset(stateItems[i].pointer, 0, stateItems[i].size);

    // Set default values
    flags         = 0x24;
    stackPointer  = 0xFF;
    interrupts[1] = true;
    inputMask     = 0;
}

uint8_t memoryRead(uint8_t *src)
{
    // Just read the value if it's not in memory; it's probably a register
    if (src < memory || src > memory + sizeof(memory))
        return *src;

    uint16_t address = src - memory;

    // Get the real location of a mirrored address in memory
    if (address >= 0x0800 && address < 0x2000)
        address %= 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + address % 8;

    if (address == 0x4016) // JOYPAD1
    {
        // Read button status 1 bit at a time
        uint8_t value = (inputMask & (1 << inputShift)) ? 0x41 : 0x40;
        ++inputShift %= 8;
        return value;
    }

    // Get a value from a memory-mapped register if needed
    if ((address >= 0x2000 && address < 0x2008) || address == 0x4014)
        return ppu::registerRead(address);
    else if (address >= 0x4000 && address < 0x4018)
        return apu::registerRead(address);
    else
        return memory[address];
}

void memoryWrite(uint8_t *dst, uint8_t src)
{
    // Just write the value if it's not in memory; it's probably a register
    if (dst < memory || dst > memory + sizeof(memory))
    {
        *dst = src;
        return;
    }

    uint16_t address = dst - memory;

    // Get the real location of a mirrored address in memory
    if (address >= 0x0800 && address < 0x2000)
        address %= 0x0800;
    else if (address >= 0x2008 && address < 0x4000)
        address = 0x2000 + address % 8;

    if (address == 0x4016) // JOYPAD1
    {
        // Reset the input shift when the strobe bit is set
        if (src & 0x01)
            inputShift = 0;
        return;
    }

    // Pass the value to a memory-mapped register if needed
    if ((address >= 0x2000 && address < 0x2008) || address == 0x4014)
        ppu::registerWrite(address, src);
    else if (address >= 0x4000 && address < 0x4018)
        apu::registerWrite(address, src);
    else if (address >= 0x8000)
        mapper::registerWrite(address, src);
    else
        memory[address] = src;

    // Suspend the CPU on a DMA transfer with an extra cycle on odd CPU cycles
    if (address == 0x4014)
        targetCycles += (core::globalCycles == 3) ? 514 : 513;
}

uint8_t *zeroPage()
{
    // Use the immediate value as a memory address
    programCounter++;
    return &memory[memory[programCounter]];
}

uint8_t *zeroPageX()
{
    // Use the immediate value plus the X register as a memory address in zero page
    programCounter++;
    return &memory[(memory[programCounter] + registerX) % 0x100];
}

uint8_t *zeroPageY()
{
    // Use the immediate value plus the Y register as a memory address in zero page
    programCounter++;
    return &memory[(memory[programCounter] + registerY) % 0x100];
}

uint8_t *absolute()
{
    // Use the immediate 2 values as a memory address
    programCounter += 2;
    return &memory[memory[programCounter - 1] | (memory[programCounter] << 8)];
}

uint8_t *absoluteX(bool pageCycle)
{
    // Use the absolute value plus the X register as a memory address
    programCounter += 2;
    uint16_t address = memory[programCounter - 1] | (memory[programCounter] << 8);
    if (pageCycle && (address & 0xFF) + registerX > 0xFF) // Page cross
        targetCycles++;
    return &memory[address + registerX];
}

uint8_t *absoluteY(bool pageCycle)
{
    // Use the absolute value plus the Y register as a memory address
    programCounter += 2;
    uint16_t address = memory[programCounter - 1] | (memory[programCounter] << 8);
    if (pageCycle && (address & 0xFF) + registerY > 0xFF) // Page cross
        targetCycles++;
    return &memory[address + registerY];
}

uint8_t *indirect()
{
    // Use the value stored at the absolute address as a memory address
    programCounter += 2;
    uint16_t addressLower = memory[programCounter - 1] | (memory[programCounter] << 8);
    uint16_t addressUpper = (addressLower & 0xFF00) | ((addressLower + 1) & 0x00FF);
    return &memory[(memory[addressUpper] << 8) | memory[addressLower]];
}

uint8_t *indirectX()
{
    // Use the value stored at the zero page X address as a memory address
    programCounter++;
    uint8_t addressLower = memory[(memory[programCounter] + registerX) % 0x100];
    uint8_t addressUpper = memory[(memory[programCounter] + registerX + 1) % 0x100];
    return &memory[(addressUpper << 8) | addressLower];
}

uint8_t *indirectY(bool pageCycle)
{
    // Use the value stored at the zero page address plus the Y register as a memory address
    programCounter++;
    uint8_t addressLower = memory[memory[programCounter]];
    uint8_t addressUpper = memory[(memory[programCounter] + 1) % 0x100];
    uint16_t address = (addressUpper << 8) | addressLower;
    if (pageCycle && (address & 0xFF) + registerY > 0xFF) // Page cross
        targetCycles++;
    return &memory[address + registerY];
}

uint8_t *immediate()
{
    // Get the value immediately after the current address
    programCounter++;
    return &memory[programCounter];
}

void cl_(uint8_t flag)
{
    // Clear a flag
    flags &= ~flag;
}

void se_(uint8_t flag)
{
    // Set a flag
    flags |= flag;
}

void ph_(uint8_t src)
{
    // Push a value to the stack
    memory[0x100 + stackPointer--] = src;
}

void pl_(uint8_t *dst)
{
    // Pull a value from the stack
    *dst = memory[0x100 + ++stackPointer];

    if (dst == &flags)
    {
        se_(0x20); // B
        cl_(0x10); // B
        return;
    }

    (*dst & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*dst == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void adc(uint8_t *src)
{
    // Add with carry
    uint8_t before = accumulator;
    uint8_t value = memoryRead(src);
    accumulator += value + (flags & 0x01);

    (accumulator & 0x80)                                                    ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)                                                      ? se_(0x02) : cl_(0x02); // Z
    ((value & 0x80) == (before & 0x80) && (flags & 0x80) != (value & 0x80)) ? se_(0x40) : cl_(0x40); // V
    (before > accumulator || value + (flags & 0x01) == 0x100)               ? se_(0x01) : cl_(0x01); // C
}

void _and(uint8_t *src)
{
    // Bitwise and
    accumulator &= memoryRead(src);

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void asl(uint8_t *dst)
{
    // Arithmetic shift left
    uint8_t before = memoryRead(dst);
    uint8_t after = before << 1;
    memoryWrite(dst, after);

    (after & 0x80)  ? se_(0x80) : cl_(0x80); // N
    (after == 0)    ? se_(0x02) : cl_(0x02); // Z
    (before & 0x80) ? se_(0x01) : cl_(0x01); // C
}

void bit(uint8_t *src)
{
    // Test bits
    uint8_t value = memoryRead(src);

    (value & 0x80)               ? se_(0x80) : cl_(0x80); // N
    (value & 0x40)               ? se_(0x40) : cl_(0x40); // V
    ((accumulator & value) == 0) ? se_(0x02) : cl_(0x02); // Z
}

void b__(bool condition)
{
    // Branch on condition
    int8_t value = *immediate();
    if (condition)
    {
        targetCycles++;
        uint16_t dst = programCounter + value + 1;
        if ((dst & 0xFF00) != ((programCounter + 1) & 0xFF00)) // Page cross
            targetCycles++;
        programCounter = dst - 1;
    }
}

void brk()
{
    // Break
    programCounter += 2;
    ph_(programCounter >> 8);
    ph_(programCounter);
    se_(0x10); // B
    ph_(flags);
    cl_(0x10); // B
    se_(0x04); // I
    programCounter = ((memory[0xFFFF] << 8) | memory[0xFFFE]) - 1;
}

void cp_(uint8_t reg, uint8_t *src)
{
    // Compare a register to a value
    uint8_t value = memoryRead(src);

    ((reg - value) & 0x80) ? se_(0x80) : cl_(0x80); // N
    (reg == value)         ? se_(0x02) : cl_(0x02); // Z
    (reg >= value)         ? se_(0x01) : cl_(0x01); // C
}

void de_(uint8_t *dst)
{
    // Decrement a value
    uint8_t value = memoryRead(dst) - 1;
    memoryWrite(dst, value);

    (value & 0x80) ? se_(0x80) : cl_(0x80); // N
    (value == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void eor(uint8_t *src)
{
    // Bitwise exclusive or
    accumulator ^= memoryRead(src);

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void in_(uint8_t *dst)
{
    // Increment a value
    uint8_t value = memoryRead(dst) + 1;
    memoryWrite(dst, value);

    (value & 0x80) ? se_(0x80) : cl_(0x80); // N
    (value == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void jmp(uint8_t *location)
{
    // Jump
    programCounter = location - memory - 1;
}

void jsr(uint8_t *location)
{
    // Jump to subroutine
    ph_(programCounter >> 8);
    ph_(programCounter);
    jmp(location);
}

void ld_(uint8_t *reg, uint8_t *src)
{
    // Load a register
    *reg = memoryRead(src);

    (*reg & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*reg == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void lsr(uint8_t *dst)
{
    // Logical shift right
    uint8_t before = memoryRead(dst);
    uint8_t after = before >> 1;
    memoryWrite(dst, after);

    (after & 0x80)  ? se_(0x80) : cl_(0x80); // N
    (after == 0)    ? se_(0x02) : cl_(0x02); // Z
    (before & 0x01) ? se_(0x01) : cl_(0x01); // C
}

void ora(uint8_t *src)
{
    // Bitwise or
    accumulator |= memoryRead(src);

    (accumulator & 0x80) ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void t__(uint8_t *src, uint8_t *dst)
{
    // Transfer one register to another
    *dst = *src;

    // Don't set flags when transferring to the stack pointer
    if (dst == &stackPointer)
        return;

    (*dst & 0x80) ? se_(0x80) : cl_(0x80); // N
    (*dst == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void rol(uint8_t *dst)
{
    // Rotate left
    uint8_t before = memoryRead(dst);
    uint8_t after = (before << 1) | (flags & 0x01);
    memoryWrite(dst, after);

    (after & 0x80)  ? se_(0x80) : cl_(0x80); // N
    (after == 0)    ? se_(0x02) : cl_(0x02); // Z
    (before & 0x80) ? se_(0x01) : cl_(0x01); // C
}

void ror(uint8_t *dst)
{
    // Rotate right
    uint8_t before = memoryRead(dst);
    uint8_t after = (before >> 1) | ((flags & 0x01) << 7);
    memoryWrite(dst, after);

    (after & 0x80)  ? se_(0x80) : cl_(0x80); // N
    (after == 0)    ? se_(0x02) : cl_(0x02); // Z
    (before & 0x01) ? se_(0x01) : cl_(0x01); // C
}

void rts()
{
    // Return from subroutine
    stackPointer += 2;
    programCounter = memory[0xFF + stackPointer] | (memory[0x100 + stackPointer] << 8);
}

void rti()
{
    // Return from interrupt
    pl_(&flags);
    rts();
    programCounter--;
}

void sbc(uint8_t *src)
{
    // Subtract with carry
    uint8_t before = accumulator;
    uint8_t value = memoryRead(src);
    accumulator -= value + !(flags & 0x01);

    (accumulator & 0x80)                                                    ? se_(0x80) : cl_(0x80); // N
    (accumulator == 0)                                                      ? se_(0x02) : cl_(0x02); // Z
    ((value & 0x80) != (before & 0x80) && (flags & 0x80) == (value & 0x80)) ? se_(0x40) : cl_(0x40); // V
    (before >= accumulator && value + !(flags & 0x01) != 0x100)             ? se_(0x01) : cl_(0x01); // C
}

void st_(uint8_t reg, uint8_t *dst)
{
    // Store a register
    memoryWrite(dst, reg);
}

void ahx(uint8_t *dst)
{
    // Store the bitwise and of the accumulator, the X register and the high byte of the address plus one
    memoryWrite(dst, accumulator & registerX & (memory[programCounter] + 1));
}

void alr(uint8_t *src)
{
    // Bitwise and and shift right
    _and(src);
    lsr(&accumulator);
}

void anc(uint8_t *src)
{
    // Bitwise and and set carry flag
    _and(src);
    (accumulator & 0x80) ? se_(0x01) : cl_(0x01); // C
}

void arr(uint8_t *src)
{
    // Bitwise and and rotate right
    _and(src);
    ror(&accumulator);

    ((accumulator & 0x40) ^ ((accumulator & 0x20) << 1)) ? se_(0x40) : cl_(0x40); // V
    (accumulator & 0x40)                                 ? se_(0x01) : cl_(0x01); // C
}

void axs(uint8_t *src)
{
    // Store the bitwise and with the X register minus a value in the X register
    registerX &= accumulator;
    uint8_t before = registerX;
    registerX -= *src;

    (registerX & 0x80)    ? se_(0x80) : cl_(0x80); // N
    (registerX == 0)      ? se_(0x02) : cl_(0x02); // Z
    (before >= registerX) ? se_(0x01) : cl_(0x01); // C
}

void dcp(uint8_t *src)
{
    // Decrement and compare
    de_(src);
    cp_(accumulator, src);
}

void isc(uint8_t *src)
{
    // Increment and subtract
    in_(src);
    sbc(src);
}

void las(uint8_t *src)
{
    // Bitwise and with the stack pointer and load multiple registers
    uint8_t value = *src & stackPointer;
    accumulator = value;
    registerX = value;
    stackPointer = value;

    (value & 0x80) ? se_(0x80) : cl_(0x80); // N
    (value == 0)   ? se_(0x02) : cl_(0x02); // Z
}

void lax(uint8_t *src)
{
    // Load the accumulator and the X register
    ld_(&accumulator, src);
    ld_(&registerX, src);
}

void rla(uint8_t *src)
{
    // Rotate left and bitwise and
    rol(src);
    _and(src);
}

void rra(uint8_t *src)
{
    // Rotate right and add
    ror(src);
    adc(src);
}

void sax(uint8_t *dst)
{
    // Bitwise and of the accumulator and the X register
    memoryWrite(dst, accumulator & registerX);
}

void shx(uint8_t *dst)
{
    // Store the bitwise and of the X register and the high byte of the address plus one
    memoryWrite(dst, registerX & (memory[programCounter] + 1));
}

void shy(uint8_t *dst)
{
    // Store the bitwise and of the Y register and the high byte of the address plus one
    memoryWrite(dst, registerY & (memory[programCounter] + 1));
}

void slo(uint8_t *src)
{
    // Shift left and bitwise or
    asl(src);
    ora(src);
}

void sre(uint8_t *src)
{
    // Shift right and bitwise exclusive or
    lsr(src);
    eor(src);
}

void tas(uint8_t *dst)
{
    // Store the bitwise and of the accumulator, the X register and the high byte of the address plus one
    stackPointer = (accumulator & registerX);
    memoryWrite(dst, stackPointer & (memory[programCounter] + 1));
}

void xaa(uint8_t *src)
{
    // Transfer the X register to the accumulator and bitwise and
    t__(&registerX, &accumulator);
    _and(src);
}

void runCycle()
{
    // Only run on a CPU cycle (3 global cycles)
    if (core::globalCycles % 3 != 0)
        return;

    // Wait until the previous instruction's cycles have finished
    if (++cycles < targetCycles)
        return;

    cycles = targetCycles = 0;

    // Disable IRQs if the inhibit flag is set
    if (interrupts[2] && (flags & 0x04))
        interrupts[2] = false;

    // Handle interrupts
    for (int i = 0; i < 3; i++)
    {
        if (interrupts[i])
        {
            ph_(programCounter >> 8);
            ph_(programCounter);
            ph_(flags);
            se_(0x04); // I
            programCounter = (memory[0xFFFB + i * 2] << 8) | memory[0xFFFA + i * 2];
            targetCycles += 7;
            interrupts[i] = false;
            return;
        }
    }

    // Decode opcode
    switch (memory[programCounter])
    {
        case 0x00: brk();                targetCycles += 7; break; // BRK
        case 0x04: zeroPage();           targetCycles += 3; break; // NOP d
        case 0x08: ph_(flags | 0x10);    targetCycles += 3; break; // PHP
        case 0x0C: absolute();           targetCycles += 4; break; // NOP a
        case 0x10: b__(!(flags & 0x80)); targetCycles += 2; break; // BPL *+d
        case 0x14: zeroPageX();          targetCycles += 4; break; // NOP d,x
        case 0x18: cl_(0x01);            targetCycles += 2; break; // CLC
        case 0x1C: absoluteX(true);      targetCycles += 4; break; // NOP a,x

        case 0x01: ora(indirectX());     targetCycles += 6; break; // ORA (d,x)
        case 0x05: ora(zeroPage());      targetCycles += 3; break; // ORA d
        case 0x09: ora(immediate());     targetCycles += 2; break; // ORA #i
        case 0x0D: ora(absolute());      targetCycles += 4; break; // ORA a
        case 0x11: ora(indirectY(true)); targetCycles += 5; break; // ORA (d),y
        case 0x15: ora(zeroPageX());     targetCycles += 4; break; // ORA d,x
        case 0x19: ora(absoluteY(true)); targetCycles += 4; break; // ORA a,y
        case 0x1D: ora(absoluteX(true)); targetCycles += 4; break; // ORA a,x

        case 0x02: return;                                          // STP
        case 0x06: asl(zeroPage());       targetCycles += 5; break; // ASL d
        case 0x0A: asl(&accumulator);     targetCycles += 2; break; // ASL
        case 0x0E: asl(absolute());       targetCycles += 6; break; // ASL a
        case 0x12: return;                                          // STP
        case 0x16: asl(zeroPageX());      targetCycles += 6; break; // ASL d,x
        case 0x1A:                        targetCycles += 2; break; // NOP
        case 0x1E: asl(absoluteX(false)); targetCycles += 7; break; // ASL a,x

        case 0x03: slo(indirectX());      targetCycles += 8; break; // SLO (d,x)
        case 0x07: slo(zeroPage());       targetCycles += 5; break; // SLO d
        case 0x0B: anc(immediate());      targetCycles += 2; break; // ANC #i
        case 0x0F: slo(absolute());       targetCycles += 6; break; // SLO a
        case 0x13: slo(indirectY(false)); targetCycles += 8; break; // SLO (d),y
        case 0x17: slo(zeroPageX());      targetCycles += 6; break; // SLO d,x
        case 0x1B: slo(absoluteY(false)); targetCycles += 7; break; // SLO a,y
        case 0x1F: slo(absoluteX(false)); targetCycles += 7; break; // SLO a,x

        case 0x20: jsr(absolute());      targetCycles += 6; break; // JSR a
        case 0x24: bit(zeroPage());      targetCycles += 3; break; // BIT d
        case 0x28: pl_(&flags);          targetCycles += 4; break; // PLP
        case 0x2C: bit(absolute());      targetCycles += 4; break; // BIT a
        case 0x30: b__( (flags & 0x80)); targetCycles += 2; break; // BMI *+d
        case 0x34: zeroPageX();          targetCycles += 4; break; // NOP d,x
        case 0x38: se_(0x01);            targetCycles += 2; break; // SEC
        case 0x3C: absoluteX(true);      targetCycles += 4; break; // NOP a,x

        case 0x21: _and(indirectX());     targetCycles += 6; break; // AND (d,x)
        case 0x25: _and(zeroPage());      targetCycles += 3; break; // AND d
        case 0x29: _and(immediate());     targetCycles += 2; break; // AND #i
        case 0x2D: _and(absolute());      targetCycles += 4; break; // AND a
        case 0x31: _and(indirectY(true)); targetCycles += 5; break; // AND (d),y
        case 0x35: _and(zeroPageX());     targetCycles += 4; break; // AND d,x
        case 0x39: _and(absoluteY(true)); targetCycles += 4; break; // AND a,y
        case 0x3D: _and(absoluteX(true)); targetCycles += 4; break; // AND a,x

        case 0x22: return;                                          // STP
        case 0x26: rol(zeroPage());       targetCycles += 5; break; // ROL d
        case 0x2A: rol(&accumulator);     targetCycles += 2; break; // ROL
        case 0x2E: rol(absolute());       targetCycles += 6; break; // ROL a
        case 0x32: return;                                          // STP
        case 0x36: rol(zeroPageX());      targetCycles += 6; break; // ROL d,x
        case 0x3A:                        targetCycles += 2; break; // NOP
        case 0x3E: rol(absoluteX(false)); targetCycles += 7; break; // ROL a,x

        case 0x23: rla(indirectX());      targetCycles += 8; break; // RLA (d,x)
        case 0x27: rla(zeroPage());       targetCycles += 5; break; // RLA d
        case 0x2B: anc(immediate());      targetCycles += 2; break; // ANC #i
        case 0x2F: rla(absolute());       targetCycles += 6; break; // RLA a
        case 0x33: rla(indirectY(false)); targetCycles += 8; break; // RLA (d),y
        case 0x37: rla(zeroPageX());      targetCycles += 6; break; // RLA d,x
        case 0x3B: rla(absoluteY(false)); targetCycles += 7; break; // RLA a,y
        case 0x3F: rla(absoluteX(false)); targetCycles += 7; break; // RLA a,x

        case 0x40: rti();                targetCycles += 6; break; // RTI
        case 0x44: zeroPage();           targetCycles += 3; break; // NOP d
        case 0x48: ph_(accumulator);     targetCycles += 3; break; // PHA
        case 0x4C: jmp(absolute());      targetCycles += 3; break; // JMP a
        case 0x50: b__(!(flags & 0x40)); targetCycles += 2; break; // BVC *+d
        case 0x54: zeroPageX();          targetCycles += 4; break; // NOP d,x
        case 0x58: cl_(0x04);            targetCycles += 2; break; // CLI
        case 0x5C: absoluteX(true);      targetCycles += 4; break; // NOP a,x

        case 0x41: eor(indirectX());     targetCycles += 6; break; // EOR (d,x)
        case 0x45: eor(zeroPage());      targetCycles += 3; break; // EOR d
        case 0x49: eor(immediate());     targetCycles += 2; break; // EOR #i
        case 0x4D: eor(absolute());      targetCycles += 4; break; // EOR a
        case 0x51: eor(indirectY(true)); targetCycles += 5; break; // EOR (d),y
        case 0x55: eor(zeroPageX());     targetCycles += 4; break; // EOR d,x
        case 0x59: eor(absoluteY(true)); targetCycles += 4; break; // EOR a,y
        case 0x5D: eor(absoluteX(true)); targetCycles += 4; break; // EOR a,x

        case 0x42: return;                                          // STP
        case 0x46: lsr(zeroPage());       targetCycles += 5; break; // LSR d
        case 0x4A: lsr(&accumulator);     targetCycles += 2; break; // LSR
        case 0x4E: lsr(absolute());       targetCycles += 6; break; // LSR a
        case 0x52: return;                                          // STP
        case 0x56: lsr(zeroPageX());      targetCycles += 6; break; // LSR d,x
        case 0x5A:                        targetCycles += 2; break; // NOP
        case 0x5E: lsr(absoluteX(false)); targetCycles += 7; break; // LSR a,x

        case 0x43: sre(indirectX());      targetCycles += 8; break; // SRE (d,x)
        case 0x47: sre(zeroPage());       targetCycles += 5; break; // SRE d
        case 0x4B: alr(immediate());      targetCycles += 2; break; // ALR #i
        case 0x4F: sre(absolute());       targetCycles += 6; break; // SRE a
        case 0x53: sre(indirectY(false)); targetCycles += 8; break; // SRE (d),y
        case 0x57: sre(zeroPageX());      targetCycles += 6; break; // SRE d,x
        case 0x5B: sre(absoluteY(false)); targetCycles += 7; break; // SRE a,y
        case 0x5F: sre(absoluteX(false)); targetCycles += 7; break; // SRE a,x

        case 0x60: rts();                targetCycles += 6; break; // RTS
        case 0x64: zeroPage();           targetCycles += 3; break; // NOP d
        case 0x68: pl_(&accumulator);    targetCycles += 4; break; // PLA
        case 0x6C: jmp(indirect());      targetCycles += 5; break; // JMP (a)
        case 0x70: b__( (flags & 0x40)); targetCycles += 2; break; // BVS *+d
        case 0x74: zeroPageX();          targetCycles += 4; break; // NOP d,x
        case 0x78: se_(0x04);            targetCycles += 2; break; // SEI
        case 0x7C: absoluteX(true);      targetCycles += 4; break; // NOP a,x

        case 0x61: adc(indirectX());     targetCycles += 6; break; // ADC (d,x)
        case 0x65: adc(zeroPage());      targetCycles += 3; break; // ADC d
        case 0x69: adc(immediate());     targetCycles += 2; break; // ADC #i
        case 0x6D: adc(absolute());      targetCycles += 4; break; // ADC a
        case 0x71: adc(indirectY(true)); targetCycles += 5; break; // ADC (d),y
        case 0x75: adc(zeroPageX());     targetCycles += 4; break; // ADC d,x
        case 0x79: adc(absoluteY(true)); targetCycles += 4; break; // ADC a,y
        case 0x7D: adc(absoluteX(true)); targetCycles += 4; break; // ADC a,x

        case 0x62: return;                                          // STP
        case 0x66: ror(zeroPage());       targetCycles += 5; break; // ROR d
        case 0x6A: ror(&accumulator);     targetCycles += 2; break; // ROR
        case 0x6E: ror(absolute());       targetCycles += 6; break; // ROR a
        case 0x72: return;                                          // STP
        case 0x76: ror(zeroPageX());      targetCycles += 6; break; // ROR d,x
        case 0x7A:                        targetCycles += 2; break; // NOP
        case 0x7E: ror(absoluteX(false)); targetCycles += 7; break; // ROR a,x

        case 0x63: rra(indirectX());      targetCycles += 8; break; // RRA (d,x)
        case 0x67: rra(zeroPage());       targetCycles += 5; break; // RRA d
        case 0x6B: arr(immediate());      targetCycles += 2; break; // ARR #i
        case 0x6F: rra(absolute());       targetCycles += 6; break; // RRA a
        case 0x73: rra(indirectY(false)); targetCycles += 8; break; // RRA (d),y
        case 0x77: rra(zeroPageX());      targetCycles += 6; break; // RRA d,x
        case 0x7B: rra(absoluteY(false)); targetCycles += 7; break; // RRA a,y
        case 0x7F: rra(absoluteX(false)); targetCycles += 7; break; // RRA a,x

        case 0x80: immediate();                   targetCycles += 2; break; // NOP #i
        case 0x84: st_(registerY, zeroPage());    targetCycles += 3; break; // STY d
        case 0x88: de_(&registerY);               targetCycles += 2; break; // DEY
        case 0x8C: st_(registerY, absolute());    targetCycles += 4; break; // STY a
        case 0x90: b__(!(flags & 0x01));          targetCycles += 2; break; // BCC *+d
        case 0x94: st_(registerY, zeroPageX());   targetCycles += 4; break; // STY d,x
        case 0x98: t__(&registerY, &accumulator); targetCycles += 2; break; // TYA
        case 0x9C: shy(absoluteX(false));         targetCycles += 5; break; // SHY a,x

        case 0x81: st_(accumulator, indirectX());      targetCycles += 6; break; // STA (d,x)
        case 0x85: st_(accumulator, zeroPage());       targetCycles += 3; break; // STA d
        case 0x89: immediate();                        targetCycles += 2; break; // NOP #i
        case 0x8D: st_(accumulator, absolute());       targetCycles += 4; break; // STA a
        case 0x91: st_(accumulator, indirectY(false)); targetCycles += 6; break; // STA (d),y
        case 0x95: st_(accumulator, zeroPageX());      targetCycles += 4; break; // STA d,x
        case 0x99: st_(accumulator, absoluteY(false)); targetCycles += 5; break; // STA a,y
        case 0x9D: st_(accumulator, absoluteX(false)); targetCycles += 5; break; // STA a,x

        case 0x82: immediate();                    targetCycles += 2; break; // NOP #i
        case 0x86: st_(registerX, zeroPage());     targetCycles += 3; break; // STX d
        case 0x8A: t__(&registerX, &accumulator);  targetCycles += 2; break; // TXA
        case 0x8E: st_(registerX, absolute());     targetCycles += 4; break; // STX a
        case 0x92: return;                                                   // STP
        case 0x96: st_(registerX, zeroPageY());    targetCycles += 4; break; // STX d,y
        case 0x9A: t__(&registerX, &stackPointer); targetCycles += 2; break; // TXS
        case 0x9E: shx(absoluteY(false));          targetCycles += 5; break; // SHX a,y

        case 0x83: sax(indirectX());      targetCycles += 6; break; // SAX (d,x)
        case 0x87: sax(zeroPage());       targetCycles += 3; break; // SAX d
        case 0x8B: xaa(immediate());      targetCycles += 2; break; // XAA #i
        case 0x8F: sax(absolute());       targetCycles += 4; break; // SAX a
        case 0x93: ahx(indirectY(false)); targetCycles += 6; break; // AHX (d),y
        case 0x97: sax(zeroPageY());      targetCycles += 4; break; // SAX d,y
        case 0x9B: tas(absoluteY(false)); targetCycles += 5; break; // TAS a,y
        case 0x9F: ahx(absoluteY(false)); targetCycles += 5; break; // AHX a,y

        case 0xA0: ld_(&registerY, immediate());     targetCycles += 2; break; // LDY #i
        case 0xA4: ld_(&registerY, zeroPage());      targetCycles += 3; break; // LDY d
        case 0xA8: t__(&accumulator, &registerY); targetCycles += 2; break; // TAY
        case 0xAC: ld_(&registerY, absolute());      targetCycles += 4; break; // LDY a
        case 0xB0: b__( (flags & 0x01)); targetCycles += 2; break; // BCS *+d
        case 0xB4: ld_(&registerY, zeroPageX());     targetCycles += 4; break; // LDY d,x
        case 0xBC: ld_(&registerY, absoluteX(true)); targetCycles += 4; break; // LDY a,x
        case 0xB8: cl_(0x40); targetCycles += 2; break; // CLV

        case 0xA1: ld_(&accumulator, indirectX());     targetCycles += 6; break; // LDA (d,x)
        case 0xA5: ld_(&accumulator, zeroPage());      targetCycles += 3; break; // LDA d
        case 0xA9: ld_(&accumulator, immediate());     targetCycles += 2; break; // LDA #i
        case 0xAD: ld_(&accumulator, absolute());      targetCycles += 4; break; // LDA a
        case 0xB1: ld_(&accumulator, indirectY(true)); targetCycles += 5; break; // LDA (d),y
        case 0xB5: ld_(&accumulator, zeroPageX());     targetCycles += 4; break; // LDA d,x
        case 0xB9: ld_(&accumulator, absoluteY(true)); targetCycles += 4; break; // LDA a,y
        case 0xBD: ld_(&accumulator, absoluteX(true)); targetCycles += 4; break; // LDA a,x

        case 0xA2: ld_(&registerX, immediate());     targetCycles += 2; break; // LDX #i
        case 0xA6: ld_(&registerX, zeroPage());      targetCycles += 3; break; // LDX d
        case 0xAA: t__(&accumulator, &registerX);    targetCycles += 2; break; // TAX
        case 0xAE: ld_(&registerX, absolute());      targetCycles += 4; break; // LDX a
        case 0xB2: return;                                                     // STP
        case 0xBA: t__(&stackPointer, &registerX);   targetCycles += 2; break; // TSX
        case 0xB6: ld_(&registerX, zeroPageY());     targetCycles += 4; break; // LDX d,y
        case 0xBE: ld_(&registerX, absoluteY(true)); targetCycles += 4; break; // LDX a,y

        case 0xA3: lax(indirectX());     targetCycles += 6; break; // LAX (d,x)
        case 0xA7: lax(zeroPage());      targetCycles += 3; break; // LAX d
        case 0xAB: lax(immediate());     targetCycles += 2; break; // LAX #i
        case 0xAF: lax(absolute());      targetCycles += 4; break; // LAX a
        case 0xB3: lax(indirectY(true)); targetCycles += 5; break; // LAX (d),y
        case 0xB7: lax(zeroPageY());     targetCycles += 4; break; // LAX d,y
        case 0xBB: las(absoluteY(true)); targetCycles += 4; break; // LAS a,y
        case 0xBF: lax(absoluteY(true)); targetCycles += 4; break; // LAX a,y

        case 0xC0: cp_(registerY, immediate()); targetCycles += 2; break; // CPY #i
        case 0xC4: cp_(registerY, zeroPage());  targetCycles += 3; break; // CPY d
        case 0xC8: in_(&registerY);             targetCycles += 2; break; // INY
        case 0xCC: cp_(registerY, absolute());  targetCycles += 4; break; // CPY a
        case 0xD0: b__(!(flags & 0x02));        targetCycles += 2; break; // BNE *+d
        case 0xD4: zeroPageX();                 targetCycles += 4; break; // NOP d,x
        case 0xD8: cl_(0x08);                   targetCycles += 2; break; // CLD
        case 0xDC: absoluteX(true);             targetCycles += 4; break; // NOP a,x

        case 0xC1: cp_(accumulator, indirectX());     targetCycles += 6; break; // CMP (d,x)
        case 0xC5: cp_(accumulator, zeroPage());      targetCycles += 3; break; // CMP d
        case 0xC9: cp_(accumulator, immediate());     targetCycles += 2; break; // CMP #i
        case 0xCD: cp_(accumulator, absolute());      targetCycles += 4; break; // CMP a
        case 0xD1: cp_(accumulator, indirectY(true)); targetCycles += 5; break; // CMP (d),y
        case 0xD5: cp_(accumulator, zeroPageX());     targetCycles += 4; break; // CMP d,x
        case 0xD9: cp_(accumulator, absoluteY(true)); targetCycles += 4; break; // CMP a,y
        case 0xDD: cp_(accumulator, absoluteX(true)); targetCycles += 4; break; // CMP a,x

        case 0xC2: immediate();           targetCycles += 2; break; // NOP #i
        case 0xC6: de_(zeroPage());       targetCycles += 5; break; // DEC d
        case 0xCA: de_(&registerX);       targetCycles += 2; break; // DEX
        case 0xCE: de_(absolute());       targetCycles += 6; break; // DEC a
        case 0xD2: return;                                          // STP
        case 0xD6: de_(zeroPageX());      targetCycles += 6; break; // DEC d,x
        case 0xDA:                        targetCycles += 2; break; // NOP
        case 0xDE: de_(absoluteX(false)); targetCycles += 7; break; // DEC a,x

        case 0xC3: dcp(indirectX());      targetCycles += 8; break; // DCP (d,x)
        case 0xC7: dcp(zeroPage());       targetCycles += 5; break; // DCP d
        case 0xCB: axs(immediate());      targetCycles += 2; break; // AXS #i
        case 0xCF: dcp(absolute());       targetCycles += 6; break; // DCP a
        case 0xD3: dcp(indirectY(false)); targetCycles += 8; break; // DCP (d),y
        case 0xD7: dcp(zeroPageX());      targetCycles += 6; break; // DCP d,x
        case 0xDB: dcp(absoluteY(false)); targetCycles += 7; break; // DCP a,y
        case 0xDF: dcp(absoluteX(false)); targetCycles += 7; break; // DCP a,x

        case 0xE0: cp_(registerX, immediate()); targetCycles += 2; break; // CPX #i
        case 0xE4: cp_(registerX, zeroPage());  targetCycles += 3; break; // CPX d
        case 0xE8: in_(&registerX);             targetCycles += 2; break; // INX
        case 0xEC: cp_(registerX, absolute());  targetCycles += 4; break; // CPX a
        case 0xF0: b__( (flags & 0x02));        targetCycles += 2; break; // BEQ *+d
        case 0xF4: zeroPageX();                 targetCycles += 4; break; // NOP d,x
        case 0xF8: se_(0x08);                   targetCycles += 2; break; // SED
        case 0xFC: absoluteX(true);             targetCycles += 4; break; // NOP a,x

        case 0xE1: sbc(indirectX());     targetCycles += 6; break; // SBC (d,x)
        case 0xE5: sbc(zeroPage());      targetCycles += 3; break; // SBC d
        case 0xE9: sbc(immediate());     targetCycles += 2; break; // SBC #i
        case 0xED: sbc(absolute());      targetCycles += 4; break; // SBC a
        case 0xF1: sbc(indirectY(true)); targetCycles += 5; break; // SBC (d),y
        case 0xF5: sbc(zeroPageX());     targetCycles += 4; break; // SBC d,x
        case 0xF9: sbc(absoluteY(true)); targetCycles += 4; break; // SBC a,y
        case 0xFD: sbc(absoluteX(true)); targetCycles += 4; break; // SBC a,x

        case 0xE2: immediate();           targetCycles += 2; break; // NOP #i
        case 0xE6: in_(zeroPage());       targetCycles += 5; break; // INC d
        case 0xEA:                        targetCycles += 2; break; // NOP
        case 0xEE: in_(absolute());       targetCycles += 6; break; // INC a
        case 0xF2: return;                                          // STP
        case 0xF6: in_(zeroPageX());      targetCycles += 6; break; // INC d,x
        case 0xFA:                        targetCycles += 2; break; // NOP
        case 0xFE: in_(absoluteX(false)); targetCycles += 7; break; // INC a,x

        case 0xE3: isc(indirectX());      targetCycles += 8; break; // ISC (d,x)
        case 0xE7: isc(zeroPage());       targetCycles += 5; break; // ISC d
        case 0xEB: sbc(immediate());      targetCycles += 2; break; // SBC #i
        case 0xEF: isc(absolute());       targetCycles += 6; break; // ISC a
        case 0xF3: isc(indirectY(false)); targetCycles += 8; break; // ISC (d),y
        case 0xF7: isc(zeroPageX());      targetCycles += 6; break; // ISC d,x
        case 0xFB: isc(absoluteY(false)); targetCycles += 7; break; // ISC a,y
        case 0xFF: isc(absoluteX(false)); targetCycles += 7; break; // ISC a,x
    }

    programCounter++;
}

void saveState(FILE *state)
{
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fwrite(stateItems[i].pointer, 1, stateItems[i].size, state);
}

void loadState(FILE *state)
{
    for (unsigned int i = 0; i < stateItems.size(); i++)
        fread(stateItems[i].pointer, 1, stateItems[i].size, state);
}

}
