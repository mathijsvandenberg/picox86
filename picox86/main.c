#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
//#include "tmds_encode.h"
#include "tmds_encode_font_2bpp.h"

#include "cpux86.h"
#include "spiflash.h"
#include "font_8x8.h"


#define DEBUG 1

// ACTIVITY LED
#define LED_PIN 25

#define INTERRUPT_AFTER_CYCLES 1000


// VGA 640x480x60 at DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

// FONT
#include "VGA_ROM_F16.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16

uint8_t fontcolor[16] = { 	0b000000,0b000010,0b001000,0b001010,
							0b100000,0b100010,0b100100,0b101010,
							0b010101,0b010111,0b011101,0b011111,
							0b110101,0b110111,0b111101,0b111111};
uint8_t fontcolor_fg=0;
uint8_t fontcolor_bg=0;



#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
#define COLOUR_PLANE_SIZE_WORDS (CHAR_ROWS * CHAR_COLS * 4 / 32)

struct dvi_inst dvi0;

char charbuf[CHAR_ROWS * CHAR_COLS];
uint32_t colourbuf[3 * COLOUR_PLANE_SIZE_WORDS];


uint8_t VGA_ROM_F16_COLOR[4096];



int charoffset=0;
int charoffset_backup=0;


double ADC=0;
double cputemp=0;

extern cpu_t cpu;

// Some prototypes to be put in seperate header files, instead of heading it in top of the main.c
void InitPSRAM()
{
	// TBD
}

void InitFont()
{
	int a=0;
	int line=0;
	int chr=0;
	for(line=0;line<16;line++)
	{
		for(chr=0;chr<256;chr++)
		{
			VGA_ROM_F16_COLOR[a++] = VGA_ROM_F16[(chr*16)+line];
		}
	}
}


// Pixel format RGB222

static inline void SetColor(uint8_t fg, uint8_t bg) {
	fontcolor_fg = fontcolor[fg % 16];
	fontcolor_bg = fontcolor[bg % 16];
	
}


static inline void SetCharColour(uint offset, uint8_t fg, uint8_t bg) {
	uint bit_index = offset % 8 * 4;
	uint word_index = offset / 8;
	for (int plane = 0; plane < 3; ++plane) {
		uint32_t fg_bg_combined = (fg & 0x3) | (bg << 2 & 0xc);
		colourbuf[word_index] = (colourbuf[word_index] & ~(0xfu << bit_index)) | (fg_bg_combined << bit_index);
		fg >>= 2;
		bg >>= 2;
		word_index += COLOUR_PLANE_SIZE_WORDS;
	}
}

void __not_in_flash("main") core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	static uint8_t scanbuf[80];
	static uint y = 1;
	uint32_t *tmdsbuf=0;

	for (;;)
	{
		for (uint i = 0; i < CHAR_COLS; ++i)
		{
			uint c = charbuf[i + y / FONT_CHAR_HEIGHT * CHAR_COLS];
			scanbuf[i] = VGA_ROM_F16[c*16 + (y % FONT_CHAR_HEIGHT)];
		}
		queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
		tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
		queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
		y = (y + 1) % FRAME_HEIGHT;
	}
}

void __not_in_flash("main") core1_main_rgb222() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	while (true) {
		for (uint y = 0; y < FRAME_HEIGHT; ++y) {
			uint32_t *tmdsbuf;
			queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
			for (int plane = 0; plane < 3; ++plane) {
				tmds_encode_font_2bpp(
					(const uint8_t*)&charbuf[y / FONT_CHAR_HEIGHT * CHAR_COLS],
					&colourbuf[y / FONT_CHAR_HEIGHT * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
					tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
					FRAME_WIDTH,
					(const uint8_t*)&VGA_ROM_F16_COLOR[y % FONT_CHAR_HEIGHT * 256]
				);
			}
			queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
		}
	}
}



void dviprintf(char * message, ...)
{
	char buffer[80*2];
	va_list args;
	va_start(args,message);
	int len = vsprintf(buffer,message,args);
	va_end(args);

	for(int x=0;x<len;x++)
	{
		if (buffer[x] == 0x0A)
		{
			charoffset += 80- (charoffset % 80);	
		}
		else
		{
			if (charoffset > 1919) // Hit the last line (25)
			{
				memcpy(&charbuf[800],&charbuf[880],14*80); // Scroll lines 11-24 -> 10-23
				charoffset -= 80;

				// TODO: scroll the colourbuffer as well!
			}
			charbuf[charoffset] = buffer[x];
			SetCharColour(charoffset,fontcolor_fg,fontcolor_bg);
			charoffset++;
		}
	}
}

void SetCursor(int y,int x)
{
	charoffset = x+(y*CHAR_COLS);
}

void ClrScreen()
{
	for (int x=0;x<(CHAR_COLS*CHAR_ROWS);x++)
	{
		charbuf[x] = 0x20;
		SetCharColour(x,fontcolor[7],fontcolor[0]);
	}
}

void PrintRegs()
{
	SetColor(15,1);
	SetCursor(0,61);	dviprintf("\xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF");
	SetCursor(1,61);	dviprintf("\xB3 AX:%04X CS:%04X \xB3",cpu.ax,cpu.cs);
	SetCursor(2,61);	dviprintf("\xB3 BX:%04X DS:%04X \xB3",cpu.bx,cpu.ds);
	SetCursor(3,61);	dviprintf("\xB3 CX:%04X SS:%04X \xB3",cpu.cx,cpu.ss);
	SetCursor(4,61);	dviprintf("\xB3 DX:%04X ES:%04X \xB3",cpu.dx,cpu.es);
	SetCursor(5,61);	dviprintf("\xB3 SP:%04X BP:%04X \xB3",cpu.sp,cpu.bp);
	SetCursor(6,61);	dviprintf("\xB3 SI:%04X DI:%04X \xB3",cpu.si,cpu.di);
	SetCursor(7,61);	dviprintf("\xB3 IP:%04X FL:%04X \xB3",cpu.ip,cpu.flags);
	SetCursor(8,61);	dviprintf("\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9");
	SetColor(7,0);
}

int __not_in_flash("main") main() 
{
	double cnt=0;
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
	setup_default_uart();
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	
	InitFont();

	printf("Configuring DVI\n");
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DEFAULT_DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
	ClrScreen();



	printf("Core 1 start\n");
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	//multicore_launch_core1(core1_main);
	multicore_launch_core1(core1_main_rgb222);

	charbuf[0] = 0x00;
	charbuf[1] = 0x10;
	charbuf[2] = 0x20;
	charbuf[3] = 0x30;
	charbuf[4] = 0x40;
	charbuf[5] = 0x50;
	charbuf[6] = 0x60;
	charbuf[7] = 0x70;
	charbuf[8] = 0x80;
	
	SetCursor(1,0);dviprintf("123456789");
	SetCursor(2,0);dviprintf("ABCDEFGHI");
	//while (1){ __wfi(); }

	ClrScreen();

	sleep_ms(1000);
	SetColor(15,1);
	SetCursor(0,0);	dviprintf("PICOx86 PC BIOS v0.1 2021 Mathijs van den Berg");
	SetCursor(1,0);	SetColor(7,0);dviprintf("CPU: ");SetColor(15,0);dviprintf("80186 Pi Pico");
	SetCursor(2,0);	SetColor(7,0);dviprintf("VIDEO: ");SetColor(15,0);dviprintf("DVI 640x480 60Hz 1bit by Luke Wren (Wren6991)");
	for (int x=0;x<8193;x++)
	{
		SetCursor(3,0);	dviprintf("RAM: ");SetColor(15,0);dviprintf("%4d",x);SetColor(7,0);dviprintf(" KB [");SetColor(15,0);dviprintf("Espressif ESP-PSRAM64H 64Mbit PSRAM");SetColor(7,0);dviprintf("]");
		sleep_us(250);
	}
	SetCursor(4,0);	SetColor(7,0);dviprintf("FDD: 1.44 MB [");SetColor(15,0);dviprintf("ADESTO AT25SF161 16Mbit SPI Flash");SetColor(7,0);dviprintf("]");
	SetCursor(5,0);	SetColor(7,0);dviprintf("CPUTEMP: ");

		
	// Init ADC for temperature measurement on the die
	adc_init();
	adc_select_input(4);
	adc_set_temp_sensor_enabled(true);	
	
	// Init PSRAM Memory
	InitPSRAM();


	// Init FLASH Memory on SPI0 (GPIO 0,1,2,3)
	InitSPIFlash();
	uint8_t jedec[3];
	ReadJEDEC(jedec);
	SetCursor(7,0);	dviprintf("Checking SPI Flash JEDEC: %02X %02X %02X",jedec[0],jedec[1],jedec[2]);

	// Load first 512 bytes of the floppy image to 0000:7C00 for booting (BIOS IRQ 0x19)
	InitCPU();
	x86IRQ(0x19);
	SetCursor(8,0);	dviprintf("Loaded bootsector at 0000:7C00! Booting up!",cnt);
	SetCursor(10,0);
	sleep_ms(500);
	cpu.counter = 0; 

	// Infinite loop using for(;;), faster than while(1)
	for (;;)
	{
		CPUCycle();

		//dviprintf("Testing scrolling buffer %d\n",cpu.counter);


		if (cpu.counter < 0 || DEBUG)
		{
			gpio_put(LED_PIN, 1);
			charoffset_backup = charoffset;

			// Print CPU Registers
			PrintRegs();

			// Get temperature
			ADC = (adc_read()*3.3/4096.0);
			cputemp =  27 - (ADC - 0.706)/0.001721;
			SetCursor(5,9); SetColor(10,0); dviprintf("%3.2lf",cputemp);SetColor(7,0); dviprintf(" \xF8\x43");

			cpu.counter =+ INTERRUPT_AFTER_CYCLES;
			charoffset = charoffset_backup;
			gpio_put(LED_PIN, 0);
		}
	}

	// This code should never run
	__builtin_unreachable();
}
	
