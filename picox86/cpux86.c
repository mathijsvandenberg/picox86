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

#include "floppy.h"
/*
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
								0x90,
								0x60}; 
*/	

uint8_t ram[128*1024];
cpu_t cpu;

uint16_t track;
uint16_t head;
uint16_t sector;



void dviprintf(char * message, ...);


void memcpy_seg(uint16_t seg,uint16_t offset,uint8_t * src,uint32_t len)
{
	memcpy(&ram[(seg<<4)+offset],src,len);
}

void x86IRQ(int irq)
{
	switch(irq)
	{
		case 0x13: // Floppy and harddrive access
			
			// Read Disk Sectors
			if (cpu.ah == 2)
			{
				track = cpu.ch + (cpu.cl >> 6)*256;
				head = cpu.dh;
				sector = cpu.cl & 0x3F;

				dviprintf("\n[INT13,2] Read %d sector(s) from drive %d [t=%d h=%d s=%d] to %04X:%04X",cpu.al,cpu.dl,track,head,sector,cpu.es,cpu.bx);	
				
				// 180k 5.12 Floppy
				memcpy_seg(cpu.es,cpu.bx,&floppy_img[(track*4096)+sector*512],cpu.al*512);
			}

			break;

		case 0x19: // Boot OS
			// testset for instruction test
			//memcpy_seg(0,0x7c00,floppy_img,sizeof(floppy_img));

			// Uncomment if a floppy bootsector is present in floppy_img
			memcpy_seg(0,0x7c00,floppy_img,512);
			cpu.cs=0x0000;
			cpu.ds=0x3333;
			cpu.ip=0x7c00;
			break;

		default: dviprintf("Unhandled interrupt 0x%02X! (AH=%02X)\n",irq,cpu.ah);while (1){ __wfi(); }
	}
}

uint8_t opcode;
uint8_t operand;
uint8_t oi=0; // opcode byte index
uint8_t seg;

uint16_t * p1;
uint16_t * p2;
uint16_t temp;


bool lock=false;
bool OO=false;
bool AO=false;

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

void x86OUT(uint16_t address, uint16_t value)
{
	dviprintf("\n[OUT] PORT[%04X]=0x%04X",address,value);
}

void InitCPU()
{
	cpu.ax=0x5555;
	cpu.bx=0x7c00;
	cpu.cx=0x0001;
	cpu.dx=0x0000;

	cpu.si=0x0000;
	cpu.di=0xfffe;
	cpu.bp=0x0000;
	cpu.sp=0x0100;

	cpu.ds=0x0000;
	cpu.cs=0x0000;
	cpu.es=0x0000;
	cpu.ss=0x7000;

	cpu.flags = 0x0000;
	cpu.i = 1;
}
/*
inline void ModR16(uint8_t op1,uint8_t op2)
{
	// Register addressing mode
	if (((op1 >> 6) & 0x03) == 0x00
	{
		switch ((op1 >> 3) & 0x07)
		{
			case 0: p1 = &cpu.es;break;
			case 1: p1 = &cpu.cs;break;
			case 2: p1 = &cpu.ss;break;
			case 3: p1 = &cpu.ds;break;
			case 4: p1 = &cpu.es;break;
			case 5: p1 = &cpu.cs;break;
			case 6: p1 = &cpu.ss;break;
			case 7: p1 = &cpu.ds;break;

			default: break;
		}
	}
}
*/


static inline uint8_t EffectiveAddress(uint16_t * reg,uint16_t index)
{
  uint16_t address;

  switch(*reg)
  {
	  case 0: address = cpu.bx + cpu.si;break;
	  case 1: address = cpu.bx + cpu.di;break;
	  case 2: address = cpu.bp + cpu.si;break;
	  case 3: address = cpu.bp + cpu.si;break;
	  case 4: address = cpu.si + cpu.si;break;
	  case 5: address = cpu.di + cpu.si;break;
	  case 6: address = cpu.bx + cpu.si;break;
	  case 7: address = cpu.bx + cpu.si;break;

  }

  switch (seg)
  {
	  case ES: return(ram[(cpu.es << 4) + address]);
  }
  
}


inline void ModRegRMSeg(uint8_t opcode)
{
	// Register addressing mode
	if (((opcode >> 6) & 0x03) == 0x03)
	{
		switch ((opcode >> 3) & 0x07)
		{
			case ES: p1 = &cpu.es;break;
			case CS: p1 = &cpu.cs;break;
			case SS: p1 = &cpu.ss;break;
			case DS: p1 = &cpu.ds;break;
			default: break;
		}

		switch ((opcode >> 0) & 0x07)
		{
			case AX: p2 = &cpu.ax;break;
			case CX: p2 = &cpu.cx;break;
			case DX: p2 = &cpu.dx;break;
			case BX: p2 = &cpu.bx;break;
			case SP: p2 = &cpu.sp;break;
			case BP: p2 = &cpu.bp;break;
			case SI: p2 = &cpu.si;break;
			case DI: p2 = &cpu.di;break;			
			default: break;
		}
	}
}

inline void ModRegRM(uint8_t opcode)
{
	// Register addressing mode Mod=11
	if (((opcode >> 6) & 0x03) == 0x03)
	{
		switch ((opcode >> 3) & 0x07)
		{
			case AX: p1 = &cpu.ax;break;
			case CX: p1 = &cpu.cx;break;
			case DX: p1 = &cpu.dx;break;
			case BX: p1 = &cpu.bx;break;
			case SP: p1 = &cpu.sp;break;
			case BP: p1 = &cpu.bp;break;
			case SI: p1 = &cpu.si;break;
			case DI: p1 = &cpu.di;break;
			default: break;
		}

		switch ((opcode >> 0) & 0x07)
		{
			case AX: p2 = &cpu.ax;break;
			case CX: p2 = &cpu.cx;break;
			case DX: p2 = &cpu.dx;break;
			case BX: p2 = &cpu.bx;break;
			case SP: p2 = &cpu.sp;break;
			case BP: p2 = &cpu.bp;break;
			case SI: p2 = &cpu.si;break;
			case DI: p2 = &cpu.di;break;			
			default: break;
		}
	}
}

inline void CPUCycle()
{ 
	sleep_ms(100);
	opcode = ReadInstr();
	
	switch(opcode)
	{
		// Illegal
		case 0x00: sleep_ms(500);endi();break;

		// NOP
		case 0x90: endi();break;

		// OUT
		case 0xE6: x86OUT(cpu.al,ReadInstr());endi();break;
		case 0xE7: x86OUT(cpu.ax,ReadInstr());endi();break;
		case 0xEE: x86OUT(cpu.al,cpu.dx);endi();break;
		case 0xEF: x86OUT(cpu.ax,cpu.dx);endi();break;

		// JMP
		//case 0xEA: cp
		case 0xEB: cpu.ip=cpu.ip+ReadInstr()+1;endi();break;

		

		// JC
		case 0x72: if (cpu.c) { cpu.ip += (int8_t)(ReadInstr());} else {ReadInstr(); }

		//CLI
		case 0xFA: cpu.i=0;endi();break; 

		//STI
		case 0xFB: cpu.i=1;endi();break; 

		// CALL (rel16)
		case 0xE8: temp = ReadInstr()+256*ReadInstr();
				   push(cpu.ip);
				   cpu.ip += temp;
				   endi();break; 

		//INT
		case 0xCD: x86IRQ(ReadInstr());endi();break;

		//MOV 8B Reg to Reg
		case 0x8B: ModRegRM(ReadInstr());*p1=*p2;endi();break;

		//MOV 8C Reg to Seg
		case 0x8C: ModRegRMSeg(ReadInstr());*p2=*p1;endi();break;

		//MOV 8E Seg to Reg
		case 0x8E: ModRegRMSeg(ReadInstr());*p1=*p2;endi();break;

		// MOV AX,DS:[XXXX]
		case 0xA1: cpu.ax = ReadData(ReadInstr()+256*ReadInstr());endi();break;

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
		case 0x9C: push(cpu.flags);endi();break; 

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
		case 0x9D: pop(&cpu.flags);endi();break; 

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

		default: 
			dviprintf("Unknown opcode '%02X'. System halted!",opcode);
			while (1){ __wfi(); }
			break;
	}
	cpu.counter--;
	
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