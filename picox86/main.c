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
#include "tmds_encode.h"

#include "cpux86.h"
#include "spiflash.h"


#define DEBUG 1

// ACTIVITY LED
#define LED_PIN 25

#define INTERRUPT_AFTER_CYCLES 1000


// VGA 640x480x60 at DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_10
#define DVI_TIMING dvi_timing_640x480p_60hz

// FONT
#include "VGA_ROM_F16.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16
#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;


char charbuf[CHAR_ROWS * CHAR_COLS];
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

void __not_in_flash("main") core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	sem_acquire_blocking(&dvi_start_sem);
	dvi_start(&dvi0);
	static uint8_t scanbuf[80];
	static uint y = 1;
	uint32_t *tmdsbuf=0;

	while (1) 
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
			}
			charbuf[charoffset] = buffer[x];
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
	}
}

void PrintRegs()
{
	SetCursor(0,61);	dviprintf("+----------------+");
	SetCursor(1,61);	dviprintf("|AX:%04X  CS:%04X|",cpu.ax,cpu.cs);
	SetCursor(2,61);	dviprintf("|BX:%04X  DS:%04X|",cpu.bx,cpu.ds);
	SetCursor(3,61);	dviprintf("|CX:%04X  SS:%04X|",cpu.cx,cpu.ss);
	SetCursor(4,61);	dviprintf("|DX:%04X  ES:%04X|",cpu.dx,cpu.es);
	SetCursor(5,61);	dviprintf("|SP:%04X  BP:%04X|",cpu.sp,cpu.bp);
	SetCursor(6,61);	dviprintf("|SI:%04X  DI:%04X|",cpu.si,cpu.di);
	SetCursor(7,61);	dviprintf("|IP:%04X  FL:%04X|",cpu.ip,cpu.flags);
	SetCursor(8,61);	dviprintf("+----------------+");
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
	printf("Configuring DVI\n");

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DEFAULT_DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
	ClrScreen();

	printf("Core 1 start\n");
	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);

	sem_release(&dvi_start_sem);
	sleep_ms(1000);
	SetCursor(0,0);	dviprintf("PICOx86 PC BIOS v0.1 2021 Mathijs van den Berg");
	SetCursor(1,0);	dviprintf("CPU: 80186 Pi Pico");
	SetCursor(2,0);	dviprintf("VIDEO: DVI 640x480 60Hz 1bit by Luke Wren (Wren6991)");
	for (int x=0;x<8193;x++)
	{
		SetCursor(3,0);	dviprintf("RAM: %4d KB [Espressif ESP-PSRAM64H 64Mbit PSRAM]",x);
		sleep_us(500);
	}
	SetCursor(4,0);	dviprintf("FDD: 1.44 MB [ADESTO AT25SF161 16Mbit SPI Flash]");
	SetCursor(5,0);	dviprintf("CPUTEMP: ");

	sem_release(&dvi_start_sem);
	
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
	IRQ(0x19);
	SetCursor(8,0);	dviprintf("Loaded bootsector at 0000:7C00! Booting up!",cnt);
	SetCursor(10,0);
	sleep_ms(1000);
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
			SetCursor(5,9); dviprintf("%3.2lf deg C",cputemp);

			cpu.counter =+ INTERRUPT_AFTER_CYCLES;
			charoffset = charoffset_backup;
			gpio_put(LED_PIN, 0);
		}
	}

	// This code should never run
	__builtin_unreachable();
}
	
