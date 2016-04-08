/*
    Generic helper functions to help with file I/O
*/
#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"

int read_file(char* filename, uint8_t** dataptr, uint32_t* filesize)
{
    FILE* sourcefile;
    sourcefile = fopen(filename,"rb");

    if(!sourcefile)
    {
        printf("Could not open %s\n",filename);
        perror("Error");
        return -1;
    }
    fseek(sourcefile,0,SEEK_END);
    *filesize = ftell(sourcefile);
    rewind(sourcefile);
    *dataptr = (uint8_t*)malloc((*filesize)+128);

    int32_t res = fread(*dataptr,1,*filesize,sourcefile);
    if(res != *filesize)
    {
        printf("Reading error\n");
        return -1;
    }
    fclose(sourcefile);

    return 0;
}

int write_file(char* filename, uint8_t* dataptr, uint32_t datasize)
{
    FILE *destfile;

    destfile = fopen(filename,"wb");
    if(!destfile)
    {
        printf("Could not open %s\n",filename);
        return -1;
    }
    int32_t res = fwrite(dataptr, 1, datasize, destfile);
    if(res != datasize)
    {
        printf("Writing error\n");
        return -1;
    }
    fclose(destfile);
    printf("%d bytes written to %s.\n",res,filename);
    return 0;
}
