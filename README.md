# picox86
x86 emulator on Raspberry Pi Pico

![](img/picox86pcb.jpg)
![](img/picox86screen.jpg)


Quick links:

[Wren6991 PicoDVI](https://github.com/Wren6991/PicoDVI)

[Next186](https://opencores.org/projects/next186_soc_pc/)

About this Project
-----------------

I started this project inspired by the fact that the Pi Pico can actually output DVI streams without any external component (only 8 resistors and a connector).
A simmilar project is also made by Nicolae Dumitrache with his FPGA version of a the 80186. However it would be very cool if a simple MCU can have a minimal implementation too using PIO,DMA and some trial and error.

Hardware used on bottom of PCB:

- SPI Flash chip 16Mbit (for storing FDD image to boot from, goal is that this can be programmed by connecting the pico with USB and a simple loader)
- QSPI PSRAM 64Mbit (can clock up to 133 MHz / 4bit) which results in ~ 66 Mbyte/sec sequential access, goal is to offload the access with PIO and a small cache


