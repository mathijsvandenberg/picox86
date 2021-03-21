#ifndef _CPUX86_H
#define _CPUX86_H

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"

void x86IRQ(int irq);
void InitCPU();
extern inline void CPUCycle();
static inline uint8_t ReadInstr();
static inline uint8_t ReadData(uint16_t Address);
static inline void WriteData(uint16_t Address,uint8_t Value);

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

/*
#define ADD 0
#define OR 1
#define ADC 2
#define SBB 3
#define AND 4
#define SUB 5
#define XOR 6
#define CMP 7
*/


typedef struct cpu_8086_s cpu_t;
struct cpu_8086_s {
    struct {
        union {
            uint16_t ax;
            struct {
                uint8_t al;
                uint8_t ah;
            };
        };
        union {
            uint16_t cx;
            struct {
                uint8_t cl;
                uint8_t ch;
            };
        };
        union {
            uint16_t dx;
            struct {
                uint8_t dl;
                uint8_t dh;
            };
        };
        union {
            uint16_t bx;
            struct {
                uint8_t bl;
                uint8_t bh;
            };
        };
        uint16_t sp;
        uint16_t bp;
        uint16_t si;
        uint16_t di;
    };
    struct {
        uint16_t es;
        uint16_t cs;
        uint16_t ss;
        uint16_t ds;
    };
    uint16_t ip;
    union {
        uint16_t flags;
        struct {
            uint8_t c: 1;
            uint8_t  : 1;
            uint8_t p: 1;
            uint8_t  : 1;
            uint8_t a: 1;
            uint8_t  : 1;
            uint8_t z: 1;
            uint8_t s: 1;
            uint8_t t: 1;
            uint8_t i: 1;
            uint8_t d: 1;
            uint8_t o: 1;
        };
    };
    int16_t counter;
};

#endif