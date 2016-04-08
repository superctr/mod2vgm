#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "mod2vgm.h"
#include "vgm.h"
#include "chip_opl4.h"

void opl4_write(uint8_t port, uint8_t reg, uint8_t channel, uint8_t value)
{
    int dualchip = (channel/24)<<7;
    channel %= 24;

    vgm_write(0xd0,port|dualchip,reg+channel,value);
}

uint16_t period_to_tone(uint16_t period)
{
    //double freq = 3579545.25/period;
    double freq = PAULA_CLK/period;
    int8_t oct = ceil(log2(freq/OPL4_RATE)+0.00000001d);
    double ref = OPL4_RATE*pow(2,(oct-1));
    uint16_t f_num = round((fmod(freq,ref)/ref)*1024);
    if(f_num > 1023)
    {
        f_num &= 1023;
        oct++;
    }
    return(oct<<12)|(f_num<<1)|use_ram;
}


ChipParams * opl4_define_parameters()
{
    opl4.max_channels=24;

    opl4.max_sample_entries = 256;
    if(use_ram)
        opl4.max_sample_entries = 128;

    return &opl4;
}

int opl4_init(int numchannels, uint8_t * samplerom, int samplerom_size, uint32_t startoffset)
{
    if(numchannels > 48)
    {
        numchannels=48;
    }

    int dualchip = (numchannels-1)/24;

    vgm_poke32(0x60, 33868800 | (dualchip << 30));

    vgm_datablock(0x84, samplerom_size, samplerom, 0x400000, startoffset, 0);
    if(dualchip)
        vgm_datablock(0x84, samplerom_size, samplerom, 0x400000, startoffset, dualchip<<31);

    int i, j;
    for(j=0;j<=dualchip;j++)
    {
        vgm_write(0xd0,FM1|j<<7, 0x05, 0x03);
        vgm_write(0xd0,PCM|j<<7, 0x00, 0x00);
        vgm_write(0xd0,PCM|j<<7, 0x01, 0x00);
        vgm_write(0xd0,PCM|j<<7, 0x02, 0x10);

        for(i=0;i<24;i++)
        {
            vgm_write(0xd0,PCM|j<<7, 0x20+i, use_ram); // should be 0x00 or 0x01
            vgm_write(0xd0,PCM|j<<7, 0x38+i, 0x00);
            vgm_write(0xd0,PCM|j<<7, 0x50+i, 0x00);
            vgm_write(0xd0,PCM|j<<7, 0x68+i, 0x00);
        }
    }
    return 0;
}

//header=12 bytes long
//offset = 12*128=1536
// b = datablock, d = source module file
uint32_t opl4_build_samplerom(uint8_t * destination, uint8_t * source, uint8_t * sourceend)
{
    int i;
    uint8_t * header = destination;
    uint8_t * sample_pointer = destination;
    sample_pointer += use_ram ? 1536 : 3072;
    int32_t sample_length;
    int32_t silent_loop;
    int32_t empty_sample=0;
    uint32_t opl4_start;
    uint16_t opl4_loop, opl4_end;

    Sample * sample;
    Subsample * sampleoffs;

    for(i=0;i<mod.num_samples;i++)
    {

        sample = &mod.samples[i];

        // For non-looping samples, we need to add a silent loop at the end.
        // This is because the OPL4 always wants to loop samples. If the loop value is too close to
        // the end value, the sample counter will just overflow and it will keep playing beyond the
        // loop point.
        sample_length = sample->used_length;

        if(sample_length > 0 && empty_sample == 1)
            sample_pointer += silent_loop;

        silent_loop = SILENT_LOOP_LENGTH; // Samples added to end in order to silence non-looping samples.

        // For looping samples, we can cut the sample at the loop point.
        // Also we do not need to add a silent loop at the end
        if(sample->loop_length > 2)
        {
            //sample_length = sample->loop_length+sample->loop_start;
            silent_loop = 0;
        }

        // If sample length is beyond threshold, cut the loop and trim the sample.
        if(sample_length+silent_loop>65535)
        {
            fprintf(stderr,"Warning: Sample %d length > 65535, trimming!\n", i+1);
            silent_loop = SILENT_LOOP_LENGTH;
            sample_length = 65535-silent_loop;
        }

        // Warn if we exceed EOF of the module (This should never happen unless the MOD is corrupted)
        if(source+sample_length > sourceend)
            fprintf(stderr,"Warning: sample %d exceeding end of file (%p, %p)\n",i+1, source+sample_length, sourceend);

        opl4_start = rom_offset + sample_pointer - destination;
        sample->db_position = opl4_start;

        opl4_loop = silent_loop ? sample_length : sample->loop_start;
        opl4_end = ~(sample_length+silent_loop-1);

        verbose(0,"Sample %2d %06x %04x %04x (sum=%04x, length=%d, loop=%d, silent=%d)\n",i+1,opl4_start,opl4_loop,opl4_end, opl4_loop+opl4_end, sample_length, sample->loop_length, silent_loop);

        *(header) = (opl4_start>>16)&0x3f; // start addr H
        *(header+1) = (opl4_start>>8)&0xff; // start addr M
        *(header+2) = (opl4_start>>0)&0xff; // start addr L
        *(header+3) = (opl4_loop>>8)&0xff; // loop addr H
        *(header+4) = (opl4_loop>>0)&0xff; // loop addr L
        *(header+5) = (opl4_end>>8)&0xff; // end addr H
        *(header+6) = (opl4_end>>0)&0xff; // end addr L
        *(header+7) = 0x00; // LFO/VIB
        *(header+8) = 0xFF; // AR/D1R
        *(header+9) = 0x00; // DL/D2R
        *(header+10)= 0x0F; // Rate correction/RR
        *(header+11)= 0x00; // AM
        header +=12;

        // Copy sample to datablock. Also some logic to save ROM space for songs with many empty samples.
        if(sample_length > 0)
        {
            memcpy(sample_pointer, source, sample_length);
            sample_pointer += sample_length+(SILENT_LOOP_LENGTH*2);
            empty_sample=0;
        }
        else
        {
            empty_sample=1;
        }

        source += sample->length;
    }

    for(i=0;i<mod.num_sampleoffsets;i++)
    {

        sampleoffs = &mod.sampleoffsets[i];

        // For non-looping samples, we need to add a silent loop at the end.
        // This is because the OPL4 always wants to loop samples. If the loop value is too close to
        // the end value, the sample counter will just overflow and it will keep playing beyond the
        // loop point.
        sample_length = sampleoffs->length;

        silent_loop = SILENT_LOOP_LENGTH; // Samples added to end in order to silence non-looping samples.

        // For looping samples, we can cut the sample at the loop point.
        // Also we do not need to add a silent loop at the end
        if(sampleoffs->loop_length > 2)
        {
            sample_length = sampleoffs->loop_length+sampleoffs->loop_start;
            silent_loop = 0;
        }

        if(sample_length+silent_loop>65535)
        {
            silent_loop = SILENT_LOOP_LENGTH;
            sample_length = 65535-silent_loop;
        }

        opl4_start = mod.samples[sampleoffs->sampleid].db_position + sampleoffs->start_offset;

        opl4_loop = silent_loop ? sample_length : sampleoffs->loop_start;
        opl4_end = ~(sample_length+silent_loop-1);

        verbose(0,"Sub %2d=%2d %06x %04x %04x (sum=%04x, length=%d, loop=%d, silent=%d)\n",i,sampleoffs->sampleid+1,opl4_start,opl4_loop,opl4_end, opl4_loop+opl4_end, sample_length, sampleoffs->loop_length, silent_loop);

        *(header) = (opl4_start>>16)&0x3f; // start addr H
        *(header+1) = (opl4_start>>8)&0xff; // start addr M
        *(header+2) = (opl4_start>>0)&0xff; // start addr L
        *(header+3) = (opl4_loop>>8)&0xff; // loop addr H
        *(header+4) = (opl4_loop>>0)&0xff; // loop addr L
        *(header+5) = (opl4_end>>8)&0xff; // end addr H
        *(header+6) = (opl4_end>>0)&0xff; // end addr L
        *(header+7) = 0x00; // LFO/VIB
        *(header+8) = 0xFF; // AR/D1R
        *(header+9) = 0x00; // DL/D2R
        *(header+10)= 0x0F; // Rate correction/RR
        *(header+11)= 0x00; // AM
        header +=12;
    }

    verbose(0,"sample blk length: %d\n",sample_pointer-destination);
    return sample_pointer-destination;

}

// convert volumes to OPL4 attenuation values
const uint8_t AttenuationTable[65] =
{
    0xff,
    0xc0,0xa0,0x8e,0x80,0x76,0x6e,0x66,0x60,
    0x5c,0x56,0x52,0x4e,0x4a,0x46,0x44,0x40,
    0x3e,0x3c,0x38,0x36,0x34,0x32,0x30,0x2e,
    0x2c,0x2a,0x28,0x26,0x24,0x24,0x22,0x20,
    0x20,0x1e,0x1c,0x1a,0x1a,0x18,0x16,0x16,
    0x14,0x14,0x12,0x12,0x10,0x10,0x0e,0x0e,
    0x0c,0x0c,0x0a,0x0a,0x08,0x08,0x08,0x06,
    0x06,0x04,0x04,0x04,0x02,0x02,0x02,0x00
};

// 1 sample wait should be enough when setting a register.
//14 samples wait when changing sample.

void opl4_update_keyon(int c)
{
    uint8_t val;

    // Setting pan to 8 mutes a channel.
    if(song.channels[c].silenced)
        val = (song.channels[c].keyon<<7) | 0x08;
    else
        val = (song.channels[c].keyon<<7) | (song.channels[c].pan&0x0f);

    opl4_write(PCM,0x68,c,val);
    verbose(1,"\tOPL4 Ch %d Keyon = %02x\n",c,val);
    //vgm_delay(10);
}
void opl4_update_sample(int c, uint8_t sample_id)
{
    opl4_write(PCM,0x08,c,(use_ram)<<7 | sample_id);
    verbose(1,"\tOPL4 Ch %d Sample = %02x\n",c,(use_ram)<<7 | sample_id);
    //vgm_delay(140);
}

// chip->volume[i] = 32768 * pow(2.0, (-0.375 / 6) * i);

void opl4_update_volume(int c)
{
    if(song.channels[c].volume < 0)
        song.channels[c].volume = 0;
    if(song.channels[c].volume > 64)
        song.channels[c].volume = 64;
    uint8_t d = AttenuationTable[song.channels[c].volume];

    opl4_write(PCM,0x50,c,d&0xfe);

    if(song.channels[c].volume <= 0x20)

    verbose(1,"\tOPL4 Ch %d Volume = %02x (%02x)\n",c,d, song.channels[c].volume);
}
void opl4_update_freq(int c, int16_t period)
{
    uint16_t val = period_to_tone(period);
    // Does it matter in which order I write these? Application manual says it doesn't matter...
    opl4_write(PCM,0x20,c,(val&0x00ff) >> 0);
    opl4_write(PCM,0x38,c,(val&0xff00) >> 8);
    verbose(1,"\tOPL4 Ch %d Freq = %04x (%d)\n",c,val,period);
}
