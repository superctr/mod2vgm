#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include <time.h>

#include "vgm.h"
#include "fileio.h"

#include "format_mod.h"

    uint32_t delayq;
    uint32_t samplecnt;
    uint32_t loop_set;
    uint8_t* vgmdata;
    uint8_t* data;
    char* filename;

// Increments destination pointer
void my_memcpy(uint8_t** dest, void* src, int size)
{
    memcpy(*dest,src,size);
    *dest += size;
}

void add_datablockcmd(uint8_t** dest, uint8_t dtype, uint32_t size, uint32_t romsize, uint32_t offset)
{
    **dest = 0x67;*dest+=1;
    **dest = 0x66;*dest+=1;
    **dest = dtype;*dest+=1;
    size += 8;
    my_memcpy(dest,&size,4);
    my_memcpy(dest,&romsize,4);
    my_memcpy(dest,&offset,4);
}

void add_delay(uint8_t** dest, int delay)
{
    samplecnt += delay;

    int i;

    int commandcount = floor(delay/65535);
    uint16_t finalcommand = delay%65535;

    if(commandcount)
    {
        for(i=0;i<commandcount;i++)
        {
            **dest = 0x61;*dest+=1;
            **dest = 0xff;*dest+=1;
            **dest = 0xff;*dest+=1;
        }
    }

    if(finalcommand > 16)
    {
        **dest = 0x61;*dest+=1;
        my_memcpy(dest,&finalcommand,2);
    }
    else if(finalcommand > 0)
    {
        **dest = 0x70 + finalcommand-1;
        *dest+=1;
    }
}

void vgm_open(char* fname)
{
    filename=fname;
    delayq=0;
    loop_set=0;

    vgmdata=(uint8_t*)malloc(10000000);
    data = vgmdata;

    memset(data, 0, 10000000);

    // vgm magic
    memcpy(data, "Vgm ", 4);

    // version
    data+=8;
    *data++ = 0x61;
    *data++ = 0x01;

    //data offset
    *(uint32_t*)(vgmdata+0x34)=0x100-0x34;

    // YMF278B
    //*(uint32_t*)(vgmdata+0x60)= 33868800;

    data=vgmdata+0x100;
}

// *(uint32_t*)(vgmdata+0x60)= 33868800;
void vgm_poke32(int32_t offset, uint32_t d)
{
    *(uint32_t*)(vgmdata+offset)= d;
}

/*
    add_datablockcmd(&data, 0x84, dbsize, 0x400000, startoffset);
    my_memcpy(&data, datablock, dbsize);
*/
void vgm_datablock(uint8_t dbtype, uint32_t dbsize, uint8_t* datablock, uint32_t maxsize, uint32_t startoffset, int32_t flags)
{
    add_datablockcmd(&data, dbtype, dbsize|flags, maxsize, startoffset);
    my_memcpy(&data, datablock, dbsize);
}



void vgm_setloop()
{
    if(delayq/10 > 1)
    {
        add_delay(&data,delayq/10);
        delayq=delayq%10;
    }

    loop_set = samplecnt;
    *(uint32_t*)(vgmdata+0x1c)= data-vgmdata-0x1c;
}

void vgm_write(uint8_t command, uint8_t port, uint16_t reg, uint16_t value)
{
    if(delayq/10 > 1)
    {
        add_delay(&data,delayq/10);
        delayq=delayq%10;
    }

// todo: command types ...
    *data++ = command;
    *data++ = port;
    *data++ = (reg&0xff);
    *data++ = (value&0xff);
}

void vgm_delay(uint32_t delay)
{
    delayq+=delay;
}

// https://github.com/cppformat/cppformat/pull/130/files
void gd3_write_string(char* s)
{
    size_t l;
    #if defined(_WIN32) && defined(__MINGW32__) && !defined(__NO_ISOCEXT)
        l = _snwprintf((wchar_t*)data,256,L"%S", s);
    #else
        l = swprintf((wchar_t*)data,256,L"%s", s);
    #endif // defined

    data += (l+1)*2;
}

void vgm_write_tag(char* trackname)
{
    time_t t;
    struct tm * tm;
    time(&t);
    tm = localtime(&t);
    char ts [32];
    strftime(ts,32,"%Y-%m-%d %H:%M:%S",tm);

    // Tag offset
    *(uint32_t*)(vgmdata+0x14)= data-vgmdata-0x14;

    memcpy(data, "Gd3 \x00\x01\x00\x00" , 8);
    uint8_t* len_s = data+8;
    data+=12;

    gd3_write_string(trackname); // Track name
    gd3_write_string(""); // Track name (native)
    gd3_write_string(""); // Game name
    gd3_write_string(""); // Game name (native)
    gd3_write_string("Module File"); // System name
    gd3_write_string(""); // System name (native)
    gd3_write_string(""); // Author name
    gd3_write_string(""); // Author name (native)
    gd3_write_string(ts); // Time
    gd3_write_string(""); // Pack author
    gd3_write_string("Converted with mod2vgm by ctr (Built "__DATE__" "__TIME__")"); // Notes

    *(uint32_t*)(len_s) = data-len_s-4;        // length
}

void vgm_stop()
{
    if(delayq/10 > 1)
    {
        add_delay(&data,delayq/10);
        delayq=0;
    }
    *data++ = 0x66;

    // Sample count/loop sample count
    *(uint32_t*)(vgmdata+0x18)= samplecnt;
    if(loop_set)
        *(uint32_t*)(vgmdata+0x20)= samplecnt-loop_set;
}


void vgm_close(char* modulename)
{

    // EoF offset
    *(uint32_t*)(vgmdata+0x04)= data-vgmdata-4;

    write_file(filename, vgmdata, data-vgmdata);

    free(vgmdata);
}
