#pragma once

#include <stdint.h>

//address space
//new bank size (0x4000)
#define BANK_SCOOT 14
#define NUM_BANKS(areaSize) (((areaSize) >> BANK_SCOOT) + ((areaSize) & 0x00003FFF ? 1 : 0))
#define START_BANK(address) ((address) >> BANK_SCOOT)
#define END_BANK(address, size) (START_BANK(address) + NUM_BANKS(size) - 1)
#define BANK_IN_RANGE(bank, address, size) ((bank) >= START_BANK(address) && (bank) <= END_BANK(address, size))
#define BANK_ADDRESS(bank) ((bank) << BANK_SCOOT)
#define TOTAL_MEMORY_BANKS (1 << (32 - BANK_SCOOT))//0x40000 banks for BANK_SCOOT = 14

//chip addresses and sizes
//after boot RAM is at 0x00000000,
//ROM is at 0x10000000
//and the SED1376 is at 0x1FF80000(+ 0x20000 for framebuffer)
#define EMU_START_ADDRESS 0xFFFC0000
#define REG_START_ADDRESS 0xFFFFF000
#define SUPERMASSIVE_RAM_SIZE (128 * 0x100000)//128mb RAM
#define RAM_SIZE (16 * 0x100000)//16mb RAM
#define ROM_SIZE (4 * 0x100000)//4mb ROM
#define EMU_SIZE 0x20000
#define REG_SIZE 0x1000//is actually 0xE00 without bootloader
#define BOOTLOADER_SIZE 0x200
#define SED1376_MR_BIT 0x20000

//the read/write stuff looks messy here but makes the memory access functions alot cleaner
#define BUFFER_READ_8(segment, accessAddress, mask)  segment[(accessAddress) & (mask)]
#define BUFFER_READ_16(segment, accessAddress, mask) (segment[(accessAddress) & (mask)] << 8 | segment[(accessAddress) + 1 & (mask)])
#define BUFFER_READ_32(segment, accessAddress, mask) (segment[(accessAddress) & (mask)] << 24 | segment[(accessAddress) + 1 & (mask)] << 16 | segment[(accessAddress) + 2 & (mask)] << 8 | segment[(accessAddress) + 3 & (mask)])
#define BUFFER_WRITE_8(segment, accessAddress, mask, value)  segment[(accessAddress) & (mask)] = (value)
#define BUFFER_WRITE_16(segment, accessAddress, mask, value) (segment[(accessAddress) & (mask)] = (value) >> 8, segment[(accessAddress) + 1 & (mask)] = (value) & 0xFF)
#define BUFFER_WRITE_32(segment, accessAddress, mask, value) (segment[(accessAddress) & (mask)] = (value) >> 24, segment[(accessAddress) + 1 & (mask)] = ((value) >> 16) & 0xFF, segment[(accessAddress) + 2 & (mask)] = ((value) >> 8) & 0xFF, segment[(accessAddress) + 3 & (mask)] = (value) & 0xFF)

extern uint8_t bankType[];

void setRegisterXXFFAccessMode();
void setRegisterFFFFAccessMode();
void setSed1376Attached(bool attached);
void resetAddressSpace();
