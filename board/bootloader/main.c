#include "bsp.h"
#include "spiFlash.h"

int main(void) {
  spiFlash_init(SYSTEM_SPI_0_IO_CTRL, 0);
  spiFlash_wake(SYSTEM_SPI_0_IO_CTRL, 0);
  uint32_t header[2];
  spiFlash_f2m(SYSTEM_SPI_0_IO_CTRL, 0, 0x37fff0, (uint32_t)header, sizeof(header));
  if(header[0]!=0x31413656 || !header[1] || header[1]>0x480000)while(1) {}
  spiFlash_f2m(SYSTEM_SPI_0_IO_CTRL, 0, 0x380000, 0x1000, header[1]);
  asm volatile("fence rw,rw\n.word 0x500f\nfence.i" ::: "memory");
  ((void (*)(void))0x1000)();
  while(1) {}
}
