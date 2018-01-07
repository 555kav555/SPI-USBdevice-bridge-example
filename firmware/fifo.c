#include "fifo.h"

void fifo_init(fifo_t *fifo, void *buf, size_t len) {
	fifo->b = buf;
	fifo->e = buf + len;
	fifo->r = buf;
	fifo->w = buf;
}

size_t fifo_toRead(fifo_t *fifo) {
	register fifo_ptr_t r = fifo->r;
	register fifo_ptr_t w = fifo->w;
	return (w < r) ? (w + (fifo->e - fifo->b) - r) : (w - r);
}

size_t fifo_toWrite(fifo_t *fifo){
	register fifo_ptr_t w = fifo->w;
	register fifo_ptr_t r = fifo->r;
	return (r <= w) ? (r + (fifo->e - fifo->b) - w - 1) : (r - w - 1);
}

size_t fifo_read(fifo_t *fifo, void *buf, size_t len) {
	register size_t l = fifo_toRead(fifo);
	if(l > len) l = len;
	register fifo_ptr_t p = fifo->r;
	if((p + l) <= fifo->e) {
		memcpy(buf, p, l);
	} else {
		register size_t l0 = fifo->e - p;
		memcpy(buf, p, l0);
		memcpy(buf + l0, fifo->b, l - l0);
	}
	p += l;
	if(p >= fifo->e) p -= (fifo->e - fifo->b);
	fifo->r = p;
	return l;
}

size_t fifo_write(fifo_t *fifo, const void *buf, size_t len) {
	register size_t l = fifo_toWrite(fifo);
	if(l > len) l = len;
	register fifo_ptr_t p = fifo->w;
	register size_t i;
	for(i = 0; i < l; ++i) {
		*(uint8_t*)p = ((uint8_t*)buf)[i];
		++p;
		if(p == fifo->e) p = fifo->b;
	}
	fifo->w = p;
	return l;
}

fifo_ptr_t fifo_inc(fifo_t *fifo, fifo_ptr_t p, int v) {
	p += v;
	while(p >= fifo->e) p -= (fifo->e - fifo->b);
	while(p < fifo->b) p += (fifo->e - fifo->b);
	return p;
}

size_t fifo_fromEP(fifo_t *fifo, uint8_t num) {
	register fifo_ptr_t w = fifo->w;
	register fifo_ptr_t r = fifo->r;
	size_t l, len = 0;
	while(1) {
		l = (r > w) ? (r - w - 1) : (fifo->e - w);
		if(l == 0) break;
		l = USB_EP_read(num, w, l, len);
		if(l == 0) break;
		w += l;
		if(w == fifo->e) w = fifo->b;
		len += l;
	}
	fifo->w = w;
	return len;
}
