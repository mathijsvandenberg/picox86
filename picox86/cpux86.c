#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"
#include "cpux86.h"

//#include "floppy.h"
static uint8_t floppy_img[] = { 0x66, 0xB8, 0x23, 0x01, 
								0x66, 0xBB, 0x67, 0x45,
								0x66, 0xB9, 0xAB, 0x89,
								0x66, 0xBA, 0xEF, 0xCD,
								0x90,
								0xB0, 0x01,
								0xB3, 0x02,
								0xB1, 0x03,
								0xB2, 0x04,
								0x90,
								0xB4, 0x01,
								0xB7, 0x02,
								0xB5, 0x03,
								0xB6, 0x04,
								0x90 }; 
		

uint8_t ram[128*1024];
cpu_t cpu;


void dviprintf(char * message, ...);


void memcpy_seg(uint16_t seg,uint16_t offset,uint8_t * src,uint32_t len)
{
	memcpy(&ram[(seg<<4)+offset],src,len);
}

void IRQ(int irq)
{
	switch(irq)
	{
		case 0x19: // Boot OS
			// testset for instruction test
			memcpy_seg(0,0x7c00,floppy_img,sizeof(floppy_img));

			// Uncomment if a floppy bootsector is present in floppy_img
			//memcpy_seg(0,0x7c00,floppy_img,512);
			cpu.cs=0x0000;
			cpu.ds=0x0000;
			cpu.ip=0x7c00;
			break;

		default: printf("Unhandled interrupt 0x%02X!\n",irq);exit(1);
	}
}

uint8_t opcode;
uint8_t oi=0; // opcode byte index

uint8_t seg;
bool lock=false;
bool OO=false;
bool AO=false;

#define ES 0
#define CS 1
#define SS 2
#define DS 3

#define AX 0
#define CX 1
#define DX 2
#define BX 3
#define SP 4
#define BP 5
#define SI 6
#define DI 7

#define AL 0
#define CL 1
#define DL 2
#define BL 3
#define AH 4
#define CH 5
#define DH 6
#define BH 7


#define endi() oi=0


void push(uint16_t value)
{
  cpu.sp--;
  cpu.sp--;
  ram[(cpu.ss << 4) + cpu.sp]=(value >> 0) & 0xFF;
  ram[(cpu.ss << 4) + cpu.sp+1]=(value >> 8) & 0xFF;
}

void pop(uint16_t * value)
{
  *value = ram[(cpu.ss << 4) + cpu.sp] + (ram[(cpu.ss << 4) + cpu.sp+1] << 8);
  cpu.sp++;
  cpu.sp++;
}

inline void CPUCycle()
{ 
	opcode = ReadInstr();
	
	switch(opcode)
	{
		// Illegal
		case 0x00: sleep_ms(500);endi();break;

		// NOP
		case 0x90: endi();break;



		//MOV Immediate to Reg
		case 0xB0: cpu.al=ReadInstr();endi();break;
		case 0xB1: cpu.cl=ReadInstr();endi();break;
		case 0xB2: cpu.dl=ReadInstr();endi();break;
		case 0xB3: cpu.bl=ReadInstr();endi();break;
		case 0xB4: cpu.ah=ReadInstr();endi();break;
		case 0xB5: cpu.ch=ReadInstr();endi();break;
		case 0xB6: cpu.dh=ReadInstr();endi();break;
		case 0xB7: cpu.bh=ReadInstr();endi();break;

		case 0xB8: cpu.ax=ReadInstr()+256*ReadInstr();endi();break;
		case 0xB9: cpu.cx=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBA: cpu.dx=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBB: cpu.bx=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBC: cpu.sp=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBD: cpu.bp=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBE: cpu.si=ReadInstr()+256*ReadInstr();endi();break;
		case 0xBF: cpu.di=ReadInstr()+256*ReadInstr();endi();break;

    // PUSH registers
    case 0x50: push(cpu.ax);endi();break;
    case 0x51: push(cpu.cx);endi();break;
    case 0x52: push(cpu.dx);endi();break;
    case 0x53: push(cpu.bx);endi();break;
    case 0x54: push(cpu.sp);endi();break;
    case 0x55: push(cpu.bp);endi();break;
    case 0x56: push(cpu.si);endi();break;
    case 0x57: push(cpu.di);endi();break;

    // PUSH segments
    case 0x06: push(cpu.es);endi();break;
    case 0x0E: push(cpu.cs);endi();break;
    case 0x16: push(cpu.ss);endi();break;
    case 0x1e: push(cpu.ds);endi();break;

    // POP registers
    case 0x58: pop(&cpu.ax);endi();break;
    case 0x59: pop(&cpu.cx);endi();break;
    case 0x5A: pop(&cpu.dx);endi();break;
    case 0x5B: pop(&cpu.bx);endi();break;
    case 0x5C: pop(&cpu.sp);endi();break;
    case 0x5D: pop(&cpu.bp);endi();break;
    case 0x5E: pop(&cpu.si);endi();break;
    case 0x5F: pop(&cpu.di);endi();break;

    // POP segments
    case 0x07: pop(&cpu.es);endi();break;
    case 0x0F: pop(&cpu.cs);endi();break;
    case 0x17: pop(&cpu.ss);endi();break;
    case 0x1F: pop(&cpu.ds);endi();break;


		// Segment override prefix
		case 0x26: seg=ES;break;
		case 0x2E: seg=CS;break;
		case 0x36: seg=SS;break;
		case 0x3E: seg=DS;break;

		// Operand size override prefix
		case 0x66: OO=true;break;
		
		// Address size override prefix
		case 0x67: AO=true;break;

		// Repeat/lock prefix
		case 0xF0: lock=true;break; // LOCK

		// String manipulation prefixes
		case 0xF2: break; // REPNE/REPNZ
		case 0xF3: break; // REPE/REPZ

		default: break;
	}
	cpu.counter--;
	sleep_ms(500);
}


static inline uint8_t ReadInstr()
{
	if (oi==0) // New opcode to parse
	{
		dviprintf("\n[%04X:%04X] ",cpu.cs,cpu.ip);
	}
	dviprintf("%02X ",ram[(cpu.cs << 4) + cpu.ip]);
	oi++;	
  return(ram[(cpu.cs << 4) + cpu.ip++]);
}

static inline uint8_t ReadData(uint16_t Address)
{
  return(ram[(cpu.ds << 4) + Address]);
}
static inline void WriteData(uint16_t Address,uint8_t Value)
{
  ram[(cpu.ds << 4) + Address]=Value;
}