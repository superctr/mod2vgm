#ifndef CHIP_OPL4_H_INCLUDED
#define CHIP_OPL4_H_INCLUDED

#define FM0 0x00
#define FM1 0x01
#define PCM 0x02

#include "mod2vgm.h"

ChipParams opl4;

//uint16_t opl4_period_to_tone(uint16_t period);

#define OPL4_RATE 44100

ChipParams * opl4_define_parameters();
int opl4_init(int numchannels, uint8_t * samplerom, int samplerom_size, uint32_t startoffset);

uint32_t opl4_build_samplerom(uint8_t * destination, uint8_t * source, uint8_t * sourceend);

void opl4_update_keyon(int c);
void opl4_update_sample(int c, uint8_t sample_id);
void opl4_update_volume(int c);
void opl4_update_freq(int c, int16_t period);

void opl2_update_freq(int c,int keyon,int16_t period);
void opl2_update_ins(int c,uint8_t* ins);
void opl2_update_insvol(int c,uint8_t* ins,int vol);

#endif // CHIP_OPL4_H_INCLUDED
