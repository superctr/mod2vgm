/*
    Generic helper functions to help with file I/O
*/
#ifndef FILEIO_H_INCLUDED
#define FILEIO_H_INCLUDED

int read_file(char* filename, uint8_t** dataptr, uint32_t* filesize);
int write_file(char* filename, uint8_t* dataptr, uint32_t datasize);

#endif // FILEIO_H_INCLUDED
