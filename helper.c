#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "mod2vgm.h"


/* Tables taken from https://source.openmpt.org/browse/openmpt/trunk/OpenMPT/soundlib/Tables.cpp (BSD license) */

// Used to convert period values to tone/octave.
const uint16_t ProTrackerPeriodTable[6*12] =
{
    1712,1616,1524,1440,1356,1280,1208,1140,1076,1016,960,907, // Equivalent to C3 in OpenMPT
    856,808,762,720,678,640,604,570,538,508,480,453,
    428,404,381,360,339,320,302,285,269,254,240,226,
    214,202,190,180,170,160,151,143,135,127,120,113,
    107,101,95,90,85,80,75,71,67,63,60,56,
    53,50,47,45,42,40,37,35,33,31,30,28, // Equivalent to C8 in OpenMPT
};

// Used to convert note/octave to periods.
const uint16_t ProTrackerTunedPeriods[16*12] =
{
    1712,1616,1524,1440,1356,1280,1208,1140,1076,1016,960,907, // 00
    1700,1604,1514,1430,1348,1274,1202,1134,1070,1010,954,900, // 01
    1688,1592,1504,1418,1340,1264,1194,1126,1064,1004,948,894, // 02
    1676,1582,1492,1408,1330,1256,1184,1118,1056,996,940,888,  // 03
    1664,1570,1482,1398,1320,1246,1176,1110,1048,990,934,882,  // 04
    1652,1558,1472,1388,1310,1238,1168,1102,1040,982,926,874,  // 05
    1640,1548,1460,1378,1302,1228,1160,1094,1032,974,920,868,  // 06
    1628,1536,1450,1368,1292,1220,1150,1086,1026,968,914,862,  // 07
    1814,1712,1616,1524,1440,1356,1280,1208,1140,1076,1016,960,// 08
    1800,1700,1604,1514,1430,1350,1272,1202,1134,1070,1010,954,// 09
    1788,1688,1592,1504,1418,1340,1264,1194,1126,1064,1004,948,// 0A
    1774,1676,1582,1492,1408,1330,1256,1184,1118,1056,996,940, // 0B
    1762,1664,1570,1482,1398,1320,1246,1176,1110,1048,988,934, // 0C
    1750,1652,1558,1472,1388,1310,1238,1168,1102,1040,982,926, // 0D
    1736,1640,1548,1460,1378,1302,1228,1160,1094,1032,974,920, // 0E
    1724,1628,1536,1450,1368,1292,1220,1150,1086,1026,968,914  // 0F
};

const int16_t ModSinusTable[64] =
{
    0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126,
    127,126,125,122,117,112,106,98,90,81,71,60,49,37,25,12,
    0,-12,-25,-37,-49,-60,-71,-81,-90,-98,-106,-112,-117,-122,-125,-126,
    -127,-126,-125,-122,-117,-112,-106,-98,-90,-81,-71,-60,-49,-37,-25,-12,
};

const char* NoteNames[12] =
{
    "C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"
};

// read big endian word
uint16_t word(uint8_t* d)
{
    return (*d)<<8|*(d+1);
}

uint16_t note_to_period(uint8_t note, uint8_t finetune)
{
    uint8_t octave = note/12;
    uint8_t key = note%12;

    return ProTrackerTunedPeriods[key+(finetune*12)] >> octave;
}

void read_sample_offsets()
{

    int position,pattern, row, c, i, sample, len;
    uint8_t activesample[MAX_CHANNELS];
    uint8_t offsetmemory[MAX_CHANNELS];
    PatternColumn *col;

    for(position=0;position<mod.num_positions;position++)
    {
        pattern = mod.positions[position];
        for(row=0;row<64;row++)
        {
            for(c=0;c<mod.num_channels;c++)
            {
                col = &mod.patterns[pattern].cols[c][row];
                // update sample state
                if(col->sample > 0)
                    activesample[c] = col->sample-1;
                // look for new sample offset
                if(col->effect == SAMPLE_OFFSET)
                {
                    sample = activesample[c];

                    if(col->parameter == 0)
                        col->parameter = offsetmemory[c];
                    offsetmemory[c] = col->parameter;

                    len = col->parameter * 256;

                    // look for matches
                    for(i=0;i<mod.num_sampleoffsets;i++)
                    {
                        if(sample == mod.sampleoffsets[i].sampleid && col->parameter == mod.sampleoffsets[i].parameter)
                        {
                            col->effect = SUBSAMPLE;
                            col->parameter = i;
                            break;
                        }
                    }
                    // if no match found,  create new sample offset entry
                    if(i==mod.num_sampleoffsets && i< chip->max_sample_entries)
                    {

                        mod.sampleoffsets[i].sampleid = sample;
                        mod.sampleoffsets[i].parameter = col->parameter;

                        if(len > mod.samples[sample].used_length)
                            len = mod.samples[sample].used_length; // empty...

                        mod.sampleoffsets[i].start_offset = len;
                        mod.sampleoffsets[i].length = mod.samples[sample].used_length-len;

                        // have to disable loop starts before the sample offset
                        if(mod.samples[sample].loop_length <= 2 || len > mod.samples[sample].loop_start)
                        {
                            mod.sampleoffsets[i].loop_start = 0;
                            mod.sampleoffsets[i].loop_length = 0;
                        }
                        else
                        {
                            mod.sampleoffsets[i].loop_start = mod.samples[sample].loop_start-len;
                            mod.sampleoffsets[i].loop_length = mod.samples[sample].loop_length;
                        }

                        verbose(1,"sample offset added %02x %02x (Offset=%06x %06x %06x)\n", sample+1, col->parameter, len,mod.sampleoffsets[i].length, mod.samples[sample].length );

                        mod.num_sampleoffsets += 1;

                        col->effect = SUBSAMPLE;
                        col->parameter = i;
                    }
                }
            }
        }
    }

    return;

}

