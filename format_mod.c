#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "mod2vgm.h"


// https://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64#C
#define WHITESPACE 64
#define EQUALS     65
#define INVALID    66
static const uint8_t d[] = {
    65,66,66,66,66,66,66,66,66,66,64,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,62,66,66,66,63,52,53,
    54,55,56,57,58,59,60,61,66,66,66,65,66,66,66, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,66,66,66,66,66,66,26,27,28,
    29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66
};
int base64decode (uint8_t *in, size_t inLen, uint8_t *out, size_t *outLen) {
    uint8_t *end = in + inLen;
    uint8_t iter = 0;
    uint32_t buf = 0;
    size_t len = 0;

    while (in < end) {
       uint8_t c = d[*in++];

        switch (c) {
        case WHITESPACE: continue;   /* skip whitespace */
        case INVALID:    return 1;   /* invalid input, return error */
        case EQUALS:                 /* pad character, end of data */
            in = end;
            continue;
        default:
            buf = buf << 6 | c;
            iter++; // increment the number of iteration
            /* If the buffer is full, split it into bytes */
            if (iter == 4) {
                if ((len += 3) > *outLen) return 1; /* buffer overflow */
                *(out++) = (buf >> 16) & 255;
                *(out++) = (buf >> 8) & 255;
                *(out++) = buf & 255;
                buf = 0; iter = 0;

            }
        }
    }

    if (iter == 3) {
        if ((len += 2) > *outLen) return 1; /* buffer overflow */
        *(out++) = (buf >> 10) & 255;
        *(out++) = (buf >> 2) & 255;
    }
    else if (iter == 2) {
        if (++len > *outLen) return 1; /* buffer overflow */
        *(out++) = (buf >> 4) & 255;
    }

    *outLen = len; /* modify to reflect the actual output size */
    return 0;
}

void mod_parse_sample(uint8_t* d, Sample *s)
{
    s->flags=0;
    s->length = word(d+0x16)<<1;
    s->used_length=s->length;
    s->loop_start = word(d+0x1a)<<1;
    s->loop_length = word(d+0x1c)<<1;

    if(s->loop_length > 2)
    {
        s->flags |= 1;
        s->used_length=s->loop_start+s->loop_length;
    }

    s->finetune = *(d+0x18) & 0x0f;
    s->volume = *(d+0x19);

    memset(s->fm_data,0,sizeof(s->fm_data));
    if(!memcmp("FM: ",d,4))
    {
        size_t ol=16;
        base64decode(d+4,18,s->fm_data,&ol);
        printf("Fm data: \n");
        int i;
        for(i=0;i<16;i++)
            printf("%02x ",s->fm_data[i]);
        printf("\n");
    }

    verbose(0,"Sample info: l=%d, ls=%d ll=%d, f=%d, v=%d\n",s->length,s->loop_start,s->loop_length,s->finetune,s->volume);
}

void mod_parse_column(uint8_t* d, PatternColumn *c)
{
    uint16_t w1 = word(d);
    uint16_t w2 = word(d+2);

    uint16_t period = w1&0x0fff;

    c->sample = ((w1&0xf000)>>8) | ((w2&0xf000)>>12);
    uint8_t effect = (w2&0x0f00)>>8;
    uint8_t param = w2&0x00ff;

    c->note = 6 * 12; // "nothing"

    int i;
    // https://source.openmpt.org/browse/openmpt/trunk/OpenMPT/soundlib/Load_mod.cpp
    for(i = 0; i < 6 * 12; i++)
    {
        if(period >= ProTrackerPeriodTable[i])
        {
            if(period != ProTrackerPeriodTable[i] && i != 0)
            {
                uint16_t p1 = ProTrackerPeriodTable[i - 1];
                uint16_t p2 = ProTrackerPeriodTable[i];
                if(p1 - period < (period - p2))
                {
                    c->note = i-1;
                    break;
                }
            }
            c->note = i;
            break;
        }
    }

/*
    if(c->note/12 < 6)
    {
        verbose(1,"%s%d ",NoteNames[c->note%12],c->note/12);
    }
    else
    {
        verbose(1,"    ");
    }
    verbose(1,"%02x %01x%02x ",c->sample,effect,param);
*/

    c->parameter=param&0xff;
    c->effect=NO_EFFECT;
    switch(effect)
    {
    case 0x00: // arp
        if(param==0)
            c->effect=NO_EFFECT;
        else
            c->effect=ARPEGGIO;
        break;
    case 0x01: // porta up
        c->effect=PORTA;
        c->parameter=0-param;
        break;
    case 0x02: // porta down
        c->effect=PORTA;
        break;
    case 0x03: // tone porta
        c->effect=TONE_PORTA;
        break;
    case 0x04: // vibrato
        c->effect=VIBRATO;
        break;
    case 0x05: // volslide+toneporta
        c->effect=TONE_PORTA_AND_VOL_SLIDE;
        if(param&0xf0)
            c->parameter = (param&0xf0)>>4;
        else if(param&0x0f)
            c->parameter = 0-(param&0x0f);
        break;
    case 0x06:
        c->effect=VIBRATO_AND_VOL_SLIDE;
        if(param&0xf0)
            c->parameter = (param&0xf0)>>4;
        else if(param&0x0f)
            c->parameter = 0-(param&0x0f);
        break;
    case 0x07: // tremolo
        c->effect=TREMOLO;
        break;
    case 0x08: // panning
        c->effect=PAN;
        c->parameter = ((param+0x80)>>4)&0x0f;
        if(c->parameter == 8 && mod_panning == 2)
            c->parameter=9;
        break;
    case 0x09: // sample offset
        c->effect=SAMPLE_OFFSET;
        break;
    case 0x0a: // volume slide
        c->effect=VOL_SLIDE;
        if(param&0xf0)
            c->parameter = (param&0xf0)>>4;
        else if(param&0x0f)
            c->parameter = 0-(param&0x0f);
        break;
    case 0x0b:
        c->effect=POSITION_JUMP;
        break;
    case 0x0c:
        c->effect=VOL;
        break;
    case 0x0d:
        c->effect=PATTERN_BREAK;
        break;
    case 0x0e:
        c->parameter = param&0x0f;
        switch(param&0xf0)
        {
        case 0x10: // fine up
            c->effect=FINE_PORTA;
            c->parameter = 0-c->parameter;
            break;
        case 0x20: // fine down
            c->effect=FINE_PORTA;
        case 0x30: // glissando, not supported
        case 0x40: // vibrato waveform, not supported
        //case 0x70: // tremolo waveform, not supported
        //case 0xf0: // invert loop (not supported)
        default:
            break;
        case 0x50:
            c->effect=FINETUNE;
            break;
        case 0x60:
            c->effect=PATTERN_LOOP;
            break;
        case 0x80:
            c->effect=PAN;
            c->parameter = (param+0x08)&0x0f;
            if(c->parameter == 8 && mod_panning == 2)
                c->parameter=9;
            break;
        case 0x90:
            c->effect=RETRIGGER;
            break;
        case 0xa0:
            c->effect=VOL_FINE;
            break;
        case 0xb0:
            c->effect=VOL_FINE;
            c->parameter = 0-c->parameter;
            break;
        case 0xc0:
            c->effect=CUT;
            break;
        case 0xd0:
            c->effect=DELAY;
            break;
        case 0xe0:
            c->effect=PATTERN_DELAY;
            break;
        case 0xf0:
        case 0x70:
            c->effect=FM_CHANNEL;
            break;
        }

        break;
    case 0x0f:
    default:
        if(param<=0x20)
            c->effect=SPEED;
        else
            c->effect=TEMPO;
        break;
    }

}

uint8_t* mod_parse_file(uint8_t* d)
{
    memcpy(mod.name, d, 20);
    mod.name[20] = 0;

    mod.num_positions = *(d+0x3b6);
    mod.num_patterns = 0;
    int i,c,r;
    for(i=0;i<128;i++)
    {
        uint8_t pos = *(d+0x3b8+i);
        mod.positions[i] = pos;
        pos+=1;
        if(pos > mod.num_patterns)
            mod.num_patterns = pos;
    }

    mod.num_channels = 4;
    uint8_t * chstr = d+0x438;

    // xCHN
    if( isdigit(chstr[0]) && chstr[1] == 'C' && chstr[2] == 'H' && chstr[3] == 'N' )
        mod.num_channels = chstr[0] - '0';

    // xxCH / xxCN
    if( isdigit(chstr[0]) && isdigit(chstr[1]) && chstr[2] == 'C' )
        mod.num_channels = 10*(chstr[0]-'0') + (chstr[1]-'0');

    // OCTA / OKTA
    if( chstr[0] == 'O' && chstr[2] == 'T' && chstr[3] == 'A' )
        mod.num_channels = 8;

    mod.num_samples = 31;
    verbose(0,"Module info:\n"
            "\tPositions = %d\n"
            "\tPatterns = %d\n"
            "\tChannels = %d\n",mod.num_positions,mod.num_patterns,mod.num_channels);

    for(i=0;i<31;i++)
    {
        mod_parse_sample(d+0x14+(i*0x1e), &mod.samples[i]);
    }

    uint8_t * p = d+0x43c;
    for(i=0;i<mod.num_patterns;i++)
    {
        verbose(1,"\nReading MOD pattern %02x\n",i);
        for(r=0;r<64;r++)
        {
            for(c=0;c<mod.num_channels;c++)
            {
                mod_parse_column(p, &mod.patterns[i].cols[c][r]);
                p+=4;
            }
            //verbose(1,"\n");
        }
    }

    return p;
}

