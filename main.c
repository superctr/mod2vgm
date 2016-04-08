#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod2vgm.h"
#include "vgm.h"
#include "fileio.h"
#include "chip_opl4.h"
#include "format_mod.h"

int main(int argc, char* argv [])
{
    char trunc_name[FILENAME_MAX], filename[FILENAME_MAX];
    char *temp;
    char *output_name;  // VGM filename
    char *input_name;

    uint8_t *source = 0;
    uint8_t *samplerom = 0;

    int has_input_name=0, has_output_name=0, write_samplerom=0;
    mod_panning=0;
    use_ram=1;
    rom_offset=0x200000;
    int i;

    for(i=1;i<argc;i++)
    {
        if(!strcmp(argv[i],"-v"))
        {
            verbose_level=1;
        }
        else if(!strcmp(argv[i],"-vv"))
        {
            verbose_level=2;
        }
        else if(!strcmp(argv[i],"-s"))
        {
            write_samplerom=1;
        }
        else if(!strcmp(argv[i],"-p"))
        {
            mod_panning=1;
        }
        else if(!strcmp(argv[i],"-pp")) // fixes 800 = 810 for OPL4 panning.
        {
            mod_panning=2;
        }
        else if(!strcmp(argv[i],"-r"))
        {
            use_ram=0;
            rom_offset=0;
        }
        else if(!has_input_name)
        {
            input_name = argv[i];
            has_input_name=1;
        }
        else if(!has_output_name)
        {
            output_name = argv[i];
            has_output_name=1;
        }
        else
            break;
	}

    if(!has_input_name)
    {
        printf("\nmod2vgm by ctr\n"
               "Converts module music to OPL4 VGMs\n\n"
               "\tusage: %s [options] input.mod [output.vgm]\n\n"
               "Options:\n"
               "\t-v\tverbose\n"
               "\t-vv\teven more verbose (suggest redirecting output to a file)\n"
               "\t-s\twrite sample ROM to file\n",argv[0]);
        return -1;
    }

    if(has_output_name)
        strcpy(trunc_name,output_name);
    else
        strcpy(trunc_name,input_name);

    temp = strrchr(trunc_name,'.');
    if(temp)
        *temp = 0;  // truncate

    uint32_t sourcefile_size = 0;

    if(read_file(input_name, &source, &sourcefile_size))
        exit(EXIT_FAILURE);

    if(sourcefile_size < 2048)
    {
        fprintf(stderr,"Size < 2048 - Probably not a valid module.\n");
        exit(EXIT_FAILURE);
    }

	chip = opl4_define_parameters();

    uint8_t* p = mod_parse_file(source);
	chip->max_sample_entries -= mod.num_samples;

    mod.num_sampleoffsets=0;
    read_sample_offsets();

    printf("%d samples\n%d sample offsets used\nTotal: %d\n",mod.num_samples,mod.num_sampleoffsets,mod.num_samples+mod.num_sampleoffsets);

    verbose(1,"Sample data starts at %08x\n", p-source);

    samplerom = (uint8_t*)malloc(2097152); // 2MB samples max

    memset(samplerom,0,2097152);

    uint32_t samplelen=opl4_build_samplerom(samplerom, p, source+sourcefile_size);

    if(write_samplerom)
    {
        sprintf(filename, "%s_samples.bin", trunc_name);
        write_file(filename,samplerom,samplelen);
    }

    sprintf(filename, "%s.vgm", trunc_name);

    //vgm_open(filename, samplerom, samplelen, rom_offset);
    vgm_open(filename);
    opl4_init(mod.num_channels, samplerom, samplelen, rom_offset);

    vgm_delay(1000);

    process_song();

    vgm_stop();
    vgm_write_tag(mod.name);
    vgm_close(mod.name);

    return 0;
}
