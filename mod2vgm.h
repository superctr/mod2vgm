#ifndef MOD2VGM_H_INCLUDED
#define MOD2VGM_H_INCLUDED

#include "stdint.h"

// verbose 0: samples, positions
// verbose 1: patterns, effects, delays, chip writes
//

#ifdef NOVERBOSE
#define verbose(level,...)
#else
#define verbose(level,...) if(verbose_level > level) printf(__VA_ARGS__)
#endif

#define SILENT_LOOP_LENGTH 4

// Should be the sum of all chips' available channels
#define MAX_CHANNELS 48

#define PAULA_CLK 3546894.6d
#define OPL4_SAMPLE_OFFSET 0
#define OPL4_SUBSAMPLE_OFFSET 32

typedef enum {
    NO_EFFECT = 0,

    /* Channel effects that will not be implemented yet */
    ARPEGGIO,                   // 0xx
    VIBRATO,                    // 4xx
    VIBRATO_WAVEFORM,           // E4x
    TREMOLO,                    // 7xx
    TREMOLO_WAVEFORM,           // E7x

    /* Channel effects */
    RETRIGGER,                  // E9x
    CUT,                        // ECx
    DELAY,                      // EDx
    FINETUNE,                   // E5x
    SAMPLE_OFFSET,              // 9xx
    SUBSAMPLE,                  // Internal effect that replaces 9xx

    PORTA,                      // 1xx, 2xx
    FINE_PORTA,                 // E1x, E2x,
    TONE_PORTA,                 // 3xx
    VOL,                        // Cxx
    VOL_SLIDE,                  // Axx
    VOL_FINE,                   // EAx, EBx
    VIBRATO_AND_VOL_SLIDE,      // 6xx
    TONE_PORTA_AND_VOL_SLIDE,   // 5xx
    PAN,                        // 8xx

    /* Song effects */
    POSITION_JUMP,              // Bxx
    PATTERN_BREAK,              // Dxx
    PATTERN_LOOP,               // E6x
    PATTERN_DELAY,              // EEx
    TEMPO,                      // Fxx
    SPEED,                      // Fxx

    FM_CHANNEL,                 // EFx

} EffectTypes;

typedef struct
{
    uint32_t used_length;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_length;
    uint8_t flags;
    uint8_t finetune;
    int8_t volume;
    uint32_t db_position;

    uint8_t fm_data[16];
} Sample;

typedef struct
{
    uint16_t sampleid;
    int16_t parameter;

    uint16_t start_offset;
    uint16_t length;
    uint16_t loop_start;
    uint16_t loop_length;
} Subsample;

typedef struct
{
    uint8_t note;
//  uint8_t volumetype; // volume column effect
//  uint8_t volume;     // volume column parameter
//  uint8_t instrument;
    uint8_t sample;
    EffectTypes effect;
    int16_t parameter;

} PatternColumn;

typedef struct
{
    PatternColumn cols[MAX_CHANNELS][64];
} Pattern;

typedef struct
{
    char name[32]; // actually 20 chars.

    uint8_t num_channels;
    uint8_t num_samples;
    uint8_t num_patterns;
    uint8_t num_positions;
    uint16_t num_sampleoffsets;

    Sample samples[256];
    Subsample sampleoffsets[1024];
//  Instrument instruments[256];

    uint8_t positions[128];
    Pattern patterns[128];

} Module;

// All chip-specific things will be moved to this struct.
// Including pointers to chip functions....
typedef struct
{
    uint32_t max_channels;
    uint32_t max_sample_entries;
    uint32_t max_sample_offsets;

    uint32_t max_sample_size;
} ChipParams;


#define UPDATE_STATUS 0x01
#define SET_KEYOFF 0x02
#define SET_KEYON 0x04
#define SET_RETRIGGER 0x07
#define UPDATE_NOTE 0x08
#define UPDATE_PERIOD 0x10
#define UPDATE_VOLUME 0x20
#define UPDATE_SAMPLE 0x40

// Forces new sample on next key on.
#define KEYON_SAMPLE_UPDATE 0x100
// Active as long as a sample is playing
#define CHANNEL_ACTIVE 0x8000

/*
 if tone is set
   if sample is set
     key off, change sample, reset volume/finetune and key on.
   if sample is not set
     retrigger note (volume, finetune etc is not reset)
 if tone is not set
   if sample is set
     equal to last, reset volume and finetune
     not equal to last, key off, change sample, reset volume/finetune and key-on

 Key on must be done last (so that initial effects can be done).


 Effect priority:

 Before key on
     PATTERN_DELAY
     DELAY
     TEMPO
     SPEED
 During key on
     PATTERN_BREAK
     PATTERN_LOOP
     CUT
     FINETUNE
     VOL
     VOL_FINE
     TONE_PORTA
     PAN
 During ticks
     CUT
     ARPEGGIO
     VIBRATO
     TREMOLO
     PORTA
     TONE_PORTA
     VIBRATO_AND_VOL_SLIDE
     TONE_PORTA_AND_VOL_SLIDE
     VOL_SLIDE
*/

typedef struct
{
    uint16_t status;

    // Channel settings
    int32_t period;         // Period
    int32_t old_period;
    int32_t period_target;

    uint16_t update_flags;
    uint8_t note;
    uint8_t new_note;
    uint8_t sample;
    uint8_t new_sample;

    EffectTypes effect;
    int16_t parameter;

    // Sample settings (can be modified by effects)
    int8_t volume;
    int8_t old_volume;
    uint8_t finetune;

    // Effect settings
    int16_t porta_value;
    int16_t toneporta_value;
    int16_t volslide_value;
    uint8_t arpeggio_value;
    uint8_t arpeggio_offset;

    int32_t vibrato_offset;

    int8_t vibrato_counter;
    int8_t vibrato_amplitude;
    int8_t vibrato_speed;

    int8_t tick_counter;
    uint8_t silenced;
    uint8_t pan;

    int16_t subsample;

    int32_t offset;
    int32_t max_offset;

    uint8_t keyon;  // will be 0 on start...
    // no idea whether we'll use vibrato or not.

    uint8_t fm_channel; // >0 to enable FM

} ChannelState;

typedef struct
{
    uint8_t position;
    uint8_t pattern;
    uint8_t row;
    uint8_t next_position;   // if > max, end song
    uint8_t next_row;       // if > 64, increase pattern
    uint8_t loop_position;  // only support looping on row 0.

    uint8_t repeat;         // Should be reset every new pattern
    uint8_t repeat_counter; // Should be reset every new pattern

    uint8_t tempo;
    uint8_t ticks;
    int8_t tick_counter; // used by some effects

    uint16_t delay_sum;
    uint8_t force_update;

    int8_t loop_counter; // loop
    int8_t reached_end;
    ChannelState channels[MAX_CHANNELS];
} SongState;


const uint16_t ProTrackerPeriodTable[6*12];
const uint16_t ProTrackerTunedPeriods[16*12];
const int16_t ModSinusTable[64];
const char* NoteNames[12];

// sorry for the globals
    Module mod;
    SongState song;
    ChipParams * chip;

    // Command options
    int32_t mod_panning;
    int32_t use_ram;
    uint32_t rom_offset;
    int32_t verbose_level;



// Helper functions
uint16_t word(uint8_t* d);
uint16_t note_to_period(uint8_t note, uint8_t finetune);
void read_sample_offsets();
// moved to chip_opl4.h
//uint16_t period_to_tone(uint16_t period);
//void createsamplerom(uint8_t * destination, uint8_t * source, uint8_t * sourceend);

// moved to format_mod.h
//uint8_t* parsemodule(uint8_t* d);
//void parsesample(uint8_t* d, Sample *s);
//void parsecolumn(uint8_t* d, PatternColumn *c);

// Player functions
void process_song();

#endif // MOD2VGM_H_INCLUDED
