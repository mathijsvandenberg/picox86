#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

#define FLASH_PAGE_SIZE        256
#define FLASH_SECTOR_SIZE      4096

#define FLASH_CMD_PAGE_PROGRAM 0x02
#define FLASH_CMD_READ         0x03
#define FLASH_CMD_STATUS       0x05
#define FLASH_CMD_WRITE_EN     0x06
#define FLASH_CMD_SECTOR_ERASE 0x20
#define FLASH_CMD_READ_JEDEC   0x9F

#define FLASH_STATUS_BUSY_MASK 0x01

#define PIN_MISO 0
#define PIN_CS   1
#define PIN_SCK  2
#define PIN_MOSI 3

static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

void __not_in_flash_func(flash_read)(spi_inst_t *spi, uint cs_pin, uint32_t addr, uint8_t *buf, size_t len) {
    cs_select(cs_pin);
    uint8_t cmdbuf[4] = {
            FLASH_CMD_READ,
            addr >> 16,
            addr >> 8,
            addr
    };
    spi_write_blocking(spi, cmdbuf, 4);
    spi_read_blocking(spi, 0, buf, len);
    cs_deselect(cs_pin);
}

void __not_in_flash_func(flash_write_enable)(spi_inst_t *spi, uint cs_pin) {
    cs_select(cs_pin);
    uint8_t cmd = FLASH_CMD_WRITE_EN;
    spi_write_blocking(spi, &cmd, 1);
    cs_deselect(cs_pin);
}

void __not_in_flash_func(flash_wait_done)(spi_inst_t *spi, uint cs_pin) {
    uint8_t status;
    do {
        cs_select(cs_pin);
        uint8_t buf[2] = {FLASH_CMD_STATUS, 0};
        spi_write_read_blocking(spi, buf, buf, 2);
        cs_deselect(cs_pin);
        status = buf[1];
    } while (status & FLASH_STATUS_BUSY_MASK);
}

void __not_in_flash_func(flash_sector_erase)(spi_inst_t *spi, uint cs_pin, uint32_t addr) {
    uint8_t cmdbuf[4] = {
            FLASH_CMD_SECTOR_ERASE,
            addr >> 16,
            addr >> 8,
            addr
    };
    flash_write_enable(spi, cs_pin);
    cs_select(cs_pin);
    spi_write_blocking(spi, cmdbuf, 4);
    cs_deselect(cs_pin);
    flash_wait_done(spi, cs_pin);
}

void __not_in_flash_func(flash_page_program)(spi_inst_t *spi, uint cs_pin, uint32_t addr, uint8_t data[]) {
    uint8_t cmdbuf[4] = {
            FLASH_CMD_PAGE_PROGRAM,
            addr >> 16,
            addr >> 8,
            addr
    };
    flash_write_enable(spi, cs_pin);
    cs_select(cs_pin);
    spi_write_blocking(spi, cmdbuf, 4);
    spi_write_blocking(spi, data, FLASH_PAGE_SIZE);
    cs_deselect(cs_pin);
    flash_wait_done(spi, cs_pin);
}

void printbuf(uint8_t buf[FLASH_PAGE_SIZE]) {
    for (int i = 0; i < FLASH_PAGE_SIZE; ++i) {
        if (i % 16 == 15)
            printf("%02x\n", buf[i]);
        else
            printf("%02x ", buf[i]);
    }
}

void InitSPIFlash()
{
    // Enable SPI 0 at 1 MHz and connect to GPIOs
    spi_init(spi0, 16*1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PIN_CS);
    gpio_put(PIN_CS, 1);
    gpio_set_dir(PIN_CS, GPIO_OUT);
   
}

void ReadJEDEC(uint8_t * jedec)
{
    cs_select(PIN_CS);
    uint8_t cmdbuf[1] = {
            FLASH_CMD_READ_JEDEC
    };
    spi_write_blocking(spi0, cmdbuf,1);
    spi_read_blocking(spi0, 0, jedec, 3);
    cs_deselect(PIN_CS);

}