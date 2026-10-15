#ifndef XF_SOUND_HOST_H
#define XF_SOUND_HOST_H
#include <stdint.h>
#include <string.h>
#include <xenon_sound/sound.h>
uint32_t read32(uintptr_t);
void write32(uintptr_t,uint32_t);
void memdcbst(void *,int);
void memdcbf(void *,int);
void xenon_gpio_control(int,int,int);
int xenon_smc_i2c_write(int,int);
void xenon_smc_send_message(const unsigned char *);
#endif
