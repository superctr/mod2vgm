#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "mod2vgm.h"
#include "vgm.h"
#include "chip_opl4.h"


// Scan through module to find a loop.
int find_loop()
{
    int position,pattern, row, c;

    for(position=0;position<mod.num_positions;position++)
    {
        pattern = mod.positions[position];
        for(row=0;row<64;row++)
        {
            for(c=0;c<mod.num_channels;c++)
            {
                // F00 - no loop (for XM only?)
                //if(mod.patterns[pattern].cols[c][row].effect == SPEED
                //   && mod.patterns[pattern].cols[c][row].parameter == 0)
                //{
                //    return -1;
                //}

                // Bxx
                if(mod.patterns[pattern].cols[c][row].effect == POSITION_JUMP
                   && mod.patterns[pattern].cols[c][row].parameter <= position)
                {
                    return mod.patterns[pattern].cols[c][row].parameter;
                }
            }
        }
    }
    return -1;
}


// Mod panning:
// 6 A A 6

void initialize_channel(int c)
{
    ChannelState *ch = &song.channels[c];

    ch->status=0;
    ch->period=0;
    ch->old_period=0;
    ch->period_target=0;
    ch->update_flags=0;
    ch->note=0;
    ch->new_note=0;
    ch->effect=NO_EFFECT;
    ch->parameter=0;
    ch->sample=0xff;
    ch->new_sample=0xff; // force update
    ch->volume=0;
    ch->silenced=0;
    ch->old_volume=0xff; // force update
    ch->finetune=0;
    ch->porta_value=0;
    ch->toneporta_value=0;
    ch->volslide_value=0;
    ch->arpeggio_value=0;
    ch->arpeggio_offset=0;
    ch->vibrato_amplitude=0;
    ch->vibrato_counter=0;
    ch->vibrato_offset=0;
    ch->vibrato_speed=0;
    ch->tick_counter=0;
    ch->subsample=-1;
    ch->keyon=0;
    ch->pan=0;

    if(mod_panning)
    {
        switch(c%4)
        {
        default:
        case 0:
        case 3:
            ch->pan=0x0d;
            break;
        case 1:
        case 2:
            ch->pan=0x03;
            break;
        }
    }

}

void process_song_effects(EffectTypes effect, int16_t param)
{
    if(effect == POSITION_JUMP)
    {
        // jump to same position is equal to a loop
        if(param == song.position)
            song.loop_counter++;

        song.next_position = param;
        song.next_row = 0;
    }
    else if(effect == PATTERN_BREAK)
    {
        song.next_position++;
        song.next_row = param;
    }
    else if(effect == PATTERN_LOOP)
    {
        if(param == 0)
            song.repeat = song.row;
        else
        {
            song.repeat_counter++;
            if(song.repeat_counter <= param)
            {
                song.next_row = song.repeat;
                song.next_position=song.position;
            }
        }
    }
    else if(effect == PATTERN_DELAY)
        song.tick_counter = song.ticks + param;
    else if(effect == TEMPO)
        song.tempo = param;
    else if(effect == SPEED)
    {
        //if(!param)
        //    param = 6;

        if(param > 0)
        {
            song.ticks = param;
            song.tick_counter = song.ticks;
        }
        //else
        //    song.reached_end=1;
    }
}

// Read a row for all channels
void read_row()
{
    int32_t c;
    for(c=0;c<mod.num_channels;c++)
    {
        ChannelState *ch = &song.channels[c];
        ch->new_note = mod.patterns[song.pattern].cols[c][song.row].note;
        ch->new_sample = mod.patterns[song.pattern].cols[c][song.row].sample;
        ch->effect = mod.patterns[song.pattern].cols[c][song.row].effect;
        ch->parameter = mod.patterns[song.pattern].cols[c][song.row].parameter;
    }
}


int16_t vibrato_offset(int c)
{
    ChannelState *ch = &song.channels[c];

    return (ModSinusTable[ch->vibrato_counter]*(ch->vibrato_amplitude))/64;
}

void do_vibrato(int c)
{
    ChannelState *ch = &song.channels[c];

    // seems to be ok now. vibrato_amplitude/15 maybe? Or that is too high still?
    ch->vibrato_offset = vibrato_offset(c);
    ch->vibrato_counter += ch->vibrato_speed;
    ch->vibrato_counter &= 0x3f;
    ch->status|=UPDATE_PERIOD;

    return;
}

void process_channel(int c)
{
    verbose(1,"* Channel %d *\n",c);

    ChannelState *ch = &song.channels[c];
    ch->status &= 0xff00;
    if(song.force_update)
        ch->sample=0xff; // oh dear

    ch->tick_counter=0;
    ch->vibrato_offset=0;

    process_song_effects(ch->effect,ch->parameter);
    if(ch->effect == DELAY && ch->parameter > 0)
            return;

    // Handle new note
    //if((ch->new_note/12)<6)
    if(ch->new_note < 72)
    {
        ch->note = ch->new_note;
        if(ch->effect != TONE_PORTA && ch->effect != TONE_PORTA_AND_VOL_SLIDE)
        {
            // If an instrument was changed prior to the new note, we need to change
            // it now.
            if(ch->status&KEYON_SAMPLE_UPDATE)
            {
                verbose(1,"\tDelayed changing to sample %d until now!\n",ch->sample);
                // But this will break the subsample update
                ch->status|=UPDATE_SAMPLE;
                ch->status ^= KEYON_SAMPLE_UPDATE;
            }
            // Force sample update if last note used sample offset and this does not.
            if(ch->subsample > -1 && ch->effect != SUBSAMPLE)
            {
                ch->subsample = -1;
                ch->status|=UPDATE_SAMPLE;
                verbose(1,"\tSubsample cleared!\n");
            }
            ch->status|=SET_RETRIGGER; // Updates key off and key on
        }
        ch->status|=UPDATE_NOTE;
        ch->vibrato_offset=0;
        ch->vibrato_counter=0;
        verbose(1,"\tNew note = %s%d\n", NoteNames[ch->note%12], ch->note/12);
    }

    // Handle new sample
    if(ch->new_sample > 0)
    {
        // We'll need MOD/XM specific code here, since XM uses instruments...

        ch->new_sample--;

        // annoying. amiga mods expect retrigger for all sample changes if the sample has already ended.
        // hard to check for that here. never retriggering and always retriggering will both break
        // songs... the hack is to check if the sample is a *looping* sample and never
        // retrigger in that case. not perfect but i don't know what else to do.
        // never retriggering breaks: gslinger.mod
        // always retriggering breaks: steelchambers2.mod, most likely

        // If not same as last sample, update key on.
        if( (ch->new_sample != ch->sample && ch->new_note < 72) ||
            (mod.num_channels == 4 && ~ch->status & CHANNEL_ACTIVE) )
        {
            ch->status|=SET_RETRIGGER|UPDATE_NOTE|UPDATE_SAMPLE;

            // Force sample update if last note used sample offset and this does not.
            if(ch->subsample > -1 && ch->effect != SUBSAMPLE)
            {
                ch->subsample = -1;
                ch->status|=UPDATE_SAMPLE;
                verbose(1,"\tSubsample cleared!\n");
            }
        }
        // If we did not receive a new note, delay sample update.
        // if(mod.num_channels > 4 && ch->new_note >= 72)
        else if(ch->new_sample != ch->sample)
            ch->status|=KEYON_SAMPLE_UPDATE;

        ch->sample=ch->new_sample;
        ch->finetune = mod.samples[ch->sample].finetune;
        ch->volume = mod.samples[ch->sample].volume;
        ch->status|=UPDATE_VOLUME;
        verbose(1,"\tNew sample = %02x = (Tune:%d, Volume:%d)\n", ch->sample, ch->finetune,ch->volume);
    }

    // If arpeggio value is set and we have no arpeggio
    if(ch->arpeggio_value && ch->effect != ARPEGGIO)
    {
        ch->status |= UPDATE_NOTE;
        ch->arpeggio_value=0;
        ch->arpeggio_offset=0;
    }

    // Handle some channel effects (we do fine porta later)
    switch(ch->effect)
    {
    case ARPEGGIO:
        ch->arpeggio_value = ch->parameter; // Sets the arpeggio flag.
        ch->arpeggio_offset=0;
        ch->status|=UPDATE_NOTE;
        break;
    case VIBRATO:
        if((ch->parameter&0x0f) != 0)
            ch->vibrato_amplitude = (ch->parameter&0x0f);

        if((ch->parameter&0xf0) != 0)
            ch->vibrato_speed = (ch->parameter&0xf0)>>4;

    case VIBRATO_AND_VOL_SLIDE:
        // Amiga modules expect to have no vibrato offset on the first tick.
        // should be a better method for detecting this.
        if(mod.num_channels > 4)
            ch->vibrato_offset = vibrato_offset(c);

        ch->status|=UPDATE_PERIOD;

        break;
    case CUT: // Cut now (if param==0)
        if(ch->parameter == 0)
        {
            ch->volume=0;
            ch->status|=UPDATE_VOLUME;
        }
        break;
    case VOL: // Set volume
        ch->volume = ch->parameter;
        ch->status|=UPDATE_VOLUME;
        break;
    case VOL_FINE: // Modify volume
        ch->volume += ch->parameter;
        ch->status|=UPDATE_VOLUME;
        break;
    case FINETUNE:
        ch->finetune = ch->parameter;
        ch->status|=UPDATE_NOTE;;
        break;
    case FINE_PORTA:
        // This effect is handled in execute_tick()
        ch->status|=UPDATE_PERIOD;
        break;
    case PAN:
        ch->pan=ch->parameter;
        ch->status|=UPDATE_STATUS;
        break;
    case SUBSAMPLE:
        if(ch->subsample != ch->parameter)
        {
            verbose(1,"\nSubsample changed to %d !!\n",ch->parameter);
            ch->subsample = ch->parameter;
            // not perfect. protracker will play the next note with
            // sample offset if there is no instrument column, once.
            if(ch->new_note < 72 || ch->new_sample > 0)
                ch->status |= UPDATE_SAMPLE;
            else
                ch->status |= KEYON_SAMPLE_UPDATE;
        }
    default:
        break;
    }

}

void do_toneporta(int c)
{

    ChannelState *ch = &song.channels[c];

    verbose(1,"\tCh %d tone porta = (Current %d, Target %d, Value %d)\n",c,ch->period, ch->period_target, ch->porta_value);

    //if(ch->period == ch->period_target)
    //    ch->porta_value=0;
    if(ch->period > ch->period_target)
    {
        ch->period -= ch->porta_value;
        if(ch->period < ch->period_target)
            ch->period = ch->period_target;
    }
    else if(ch->period < ch->period_target)
    {
        ch->period += ch->porta_value;
        if(ch->period > ch->period_target)
            ch->period = ch->period_target;
    }
}


void process_channel_tick(int c)
{

    ChannelState *ch = &song.channels[c];

    if(ch->effect == DELAY)
    {
        ch->tick_counter++;
        if(ch->tick_counter == ch->parameter)
        {
            ch->effect = NO_EFFECT;
            process_channel(c);
        }
        return;
    }

    switch(ch->effect)
    {
    case ARPEGGIO:
        ch->tick_counter = ch->tick_counter==2 ? 0 : ch->tick_counter+1;
        ch->arpeggio_offset=0;
        if(ch->tick_counter==1)
            ch->arpeggio_offset=(ch->arpeggio_value&0xf0)>>4;
        if(ch->tick_counter==2)
            ch->arpeggio_offset=ch->arpeggio_value&0x0f;
        ch->status|=UPDATE_NOTE;
        break;
    case VIBRATO:
        do_vibrato(c);
        break;
    case PORTA:
        //if(ch->parameter != 0)
        //    ch->porta_value = ch->parameter;
        ch->period += ch->parameter;

        if(ch->period<1)
            ch->period=1; // F-num: ~0x720e
        if(ch->period > 20000)
            ch->period=20000; // F-num: ~0x9038
        ch->status|=UPDATE_PERIOD;
        break;
    case TONE_PORTA:
        if(ch->parameter != 0)
            ch->porta_value = ch->parameter;
        do_toneporta(c);
        ch->status|=UPDATE_PERIOD;
        break;
    case VOL_SLIDE:
    case TONE_PORTA_AND_VOL_SLIDE:
    case VIBRATO_AND_VOL_SLIDE:
        if(ch->parameter != 0)
            ch->volslide_value = ch->parameter; //&0xff;
        if(ch->effect == TONE_PORTA_AND_VOL_SLIDE)
        {
            do_toneporta(c);
            ch->status|=UPDATE_PERIOD;
        }
        else if(ch->effect == VIBRATO_AND_VOL_SLIDE)
            do_vibrato(c);
        verbose(1,"\tCh %d Vol slide %d = %d\n",c, ch->parameter,ch->volslide_value );
        ch->volume += ch->volslide_value;
        ch->status|=UPDATE_VOLUME;
        break;
    case RETRIGGER:
        ch->tick_counter++;
        if(ch->tick_counter == ch->parameter)
        {
            ch->status|=SET_RETRIGGER;
            ch->tick_counter=0;
        }
        break;
    case CUT:
        ch->tick_counter++;
        if(ch->tick_counter == ch->parameter)
        {
            ch->volume=0;
            ch->status|=UPDATE_VOLUME;
        }
        break;
    case TREMOLO:
        break;
    default:
        break;
    }
}

void execute_tick()
{
    double d = song.tempo*24/60;
    int32_t delay = 441000/d;
    int c, a;
    ChannelState *ch;


    for(c=0;c<mod.num_channels;c++)
    {
        ch = &song.channels[c];

        // if on beginning of the loop, make sure all registers will be set
        if(song.force_update)
        {
            ch->old_period=0xffff;
            ch->old_volume=-1;
        }

        // Schedule key-offs
        if(ch->status & SET_KEYOFF)
        {
            ch->keyon=0;
            opl4_update_keyon(c);
            ch->offset=0;
        }

        // Update note (octave, note, finetune)
        if(ch->status&UPDATE_NOTE)
        {

            // Some mods do tone portamento even when it has not been setup properly (ie 3xx command on new note)
            // To prevent glitches we set the target period here too.
            ch->period_target = note_to_period(ch->note+ch->arpeggio_offset, ch->finetune);
            verbose(1,"\tCh %d Period = %d (%s%d, %d)\n", c, ch->period_target, NoteNames[ch->note%12], ch->note/12, ch->finetune);

            if(ch->effect != TONE_PORTA && ch->effect != TONE_PORTA_AND_VOL_SLIDE)
                ch->period = note_to_period(ch->note+ch->arpeggio_offset, ch->finetune);
            else
            {
                verbose(1,"\t(Tone portamento)\n");
            }


            ch->status|=UPDATE_PERIOD;
        }
        // Update period (for portamento effects)
        if(ch->status&UPDATE_PERIOD)
        {
            if(ch->effect == FINE_PORTA)
            {
                ch->period += ch->parameter;
                ch->effect = NO_EFFECT; // Turn off effect to ensure we don't do the effect more than once
            }

            if(ch->period + ch->vibrato_offset != ch->old_period)
                opl4_update_freq(c,ch->period+ch->vibrato_offset);

            ch->old_period=ch->period+ch->vibrato_offset;
        }

        if(ch->status & UPDATE_VOLUME)
        {
            if(ch->volume != ch->old_volume)
                opl4_update_volume(c);
            ch->old_volume=ch->volume;
        }
    }

    for(c=0;c<mod.num_channels;c++)
    {
        ch = &song.channels[c];
        if(ch->status&UPDATE_SAMPLE)
        {
            if(ch->subsample == -1)
            {
                opl4_update_sample(c,ch->sample);
                ch->max_offset = mod.samples[ch->sample].length;
            }
            else
            {
                opl4_update_sample(c,mod.num_samples+ch->subsample);
                ch->max_offset = mod.sampleoffsets[ch->subsample].length;
            }
            ch->silenced=0;
        }
    }

    vgm_delay(150);
    delay -= 150;

    for(c=0;c<mod.num_channels;c++)
    {
        ch = &song.channels[c];

        if(ch->silenced == 0 && ch->volume == 0)
        {
            ch->silenced=1;
            ch->status|= UPDATE_STATUS;
        }
        else if(ch->silenced == 1 && ch->volume > 0)
        {
            ch->silenced=0;
            ch->status|= UPDATE_STATUS;
        }

        // Schedule key-ons
        if(ch->status & SET_KEYON)
        {
            ch->keyon=1;
            ch->status |= CHANNEL_ACTIVE;
        }
        if(ch->status & UPDATE_STATUS)
            opl4_update_keyon(c);

        if(ch->status & CHANNEL_ACTIVE)
        {
            a = mod.samples[ch->sample].loop_length;
            if(ch->subsample != -1)
                a = mod.sampleoffsets[ch->sample].loop_length;
            ch->offset += (PAULA_CLK/d)/ch->period;
            if(a <= 2 && ch->offset > ch->max_offset)
                ch->status ^= CHANNEL_ACTIVE;
        }
        ch->status&=0xFF00;
    }

    song.force_update=0;
    // Delay
    if(delay<=0)
    {
        fprintf(stderr,"Delay <= 0 !!! (Check tempo pattern %d row %02x)\n",song.pattern,song.row);
    }
    else
        vgm_delay(delay);
    verbose(1,"* Delay = %f *\n", 44100/d);
}

void process_row()
{

    int i=0,t=1;
    song.tick_counter = song.ticks;
    song.next_row = song.row + 1;
    song.pattern = mod.positions[song.position];

    if(song.loop_position == song.position)
    {
        vgm_setloop();
        song.loop_position = 0xff;
        song.force_update = 1;
    }

    verbose(1,"====================================\n");
    verbose(1,"%02x = Pattern %d Row %02x \n",song.position,song.pattern,song.row);
    verbose(1,"====================================\n");

    read_row();

    for(i=0;i<mod.num_channels;i++)
        process_channel(i);
    execute_tick();

    while(t<song.tick_counter)
    {
        for(i=0;i<mod.num_channels;i++)
            process_channel_tick(i);
        execute_tick();
        t++;
    }

    if(song.next_row > 63)
    {
        song.next_row=0;
        song.next_position++;
    }
    if(song.next_position < song.position)
    {
        song.loop_counter++;
    }
    if(song.next_position >= mod.num_positions)
    {
        song.next_position=0;
        song.loop_counter++;
    }

    song.position = song.next_position;
    song.row = song.next_row;
}

void process_song()
{
    int i;
    verbose(-1,"Now generating VGM... \n");

    song.tempo = 125;
    song.ticks = 6;
    song.tick_counter=0;
    song.loop_counter=0;
    song.pattern=0;
    song.position=0;
    song.next_position=0;
    song.row=0;
    song.delay_sum=0;
    song.repeat=0;
    song.force_update=0;
    song.reached_end=0;
    song.loop_position=find_loop();

    verbose(0,"Detected loop position: %02x\n",song.loop_position);

    for(i=0;i<mod.num_channels;i++)
        initialize_channel(i);

    while(song.loop_counter < 1 && !song.reached_end)
    {
        process_row();
    }

}


