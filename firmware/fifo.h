#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f10x_usb.h"

typedef void* fifo_ptr_t;

typedef struct {
	fifo_ptr_t r;
	fifo_ptr_t w;
	fifo_ptr_t e;
	fifo_ptr_t b;
} fifo_t;

#define FIFO_DECL(N,S) uint8_t N##_buffer[S];fifo_t N={.b=N##_buffer,.e=N##_buffer+(S),.r=N##_buffer,.w=N##_buffer}
#define FIFO_OVER(N,B,S) fifo_t N={.b=(void*)(B),.e=(void*)(B)+(S),.r=(void*)(B),.w=(void*)(B)}

static inline void fifo_clear(fifo_t *fifo) {fifo->r = fifo->w;}

void fifo_init(fifo_t *fifo, void *buf, size_t len);

size_t fifo_toRead(fifo_t *fifo);
size_t fifo_toWrite(fifo_t *fifo);
size_t fifo_read(fifo_t *fifo, void *buf, size_t len);
size_t fifo_write(fifo_t *fifo, const void *buf, size_t len);
fifo_ptr_t fifo_inc(fifo_t *fifo, fifo_ptr_t p, int v);

size_t fifo_fromEP(fifo_t *fifo, uint8_t num);
