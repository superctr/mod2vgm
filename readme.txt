Current compatibility:
	
	File format
		Protracker MOD modules.
		May support XM in the future.

	MOD:
		Supports 4, 8, 16, 24 channel modules right now.
		Has to be exactly 4, 8, 16 or 24 channels.
		If something else, use openmpt to save it with the correct amount of channels.
		
		Playback may not be 100% accurate to protracker, or anything really.
	
	Effects:
		Implemented
			0xx Arpeggio
			1xx, 2xx Portamento
			3xx, 5xx Tone portamento
			4xx, 6xx Vibrato
			8xx (Subject to chip-specific limitations) Set panning
			9xx Sample offset (Subject to chip-specific limitations)
			Axx, 5xx, 6xx Volume slide
			Bxx Position jump
			Cxx Volume
			Dxx Pattern break
			E1x, E2x Fine portamento
			E5x Fine tune
			E6x Pattern loop
			E8x Set panning (see above)
			E9x Retrigger
			EAx, EBx Fine volume slide
			ECx, note cut
			EDx, Note delay
			EEx, pattern delay (Not tested yet)
			Fxx, Speed/tempo
		Not implemented
			6xx Tremolo (planned)
			E3x Glissando
			E4x Vibrato waveform (currently sine only)
			E7x Tremolo waveform
			EFx Invert loop (Chip limitations, may work for C352?)
			
Chip notes:
	OPL4 (YMF278B)
		Max 24 channels.
		Max sample length: 65535
		Max loop length: same.
		Supports 8/12/16 bit samples.
		
		Converter only supports 8-bit samples at this point.
		
		Should support all possible Amiga frequencies. Currently using PAL values as this seem to sound
		best with the modules I have.
		
		Does not support one-off samples, all samples are looping internally. A 4 byte buffer is reserved
		at the end of the sample for this purpose. This reduces the max sample length.
	
		The OPL4 has support for hardware envelopes, this feature is not used by the converter at this point.
		
		Sample metadata is stored in the data block. Sample offsets are stored by adding extra entries.
		This limits the song to 96 unique sample offsets. (sample + 9xx parameter).
		Also, looping samples with an offset further than the loop point will not loop. They might even
		cause a high frequency beep.
		
		Has an attenuation register, the maximum level does not silence the sample completely.
		Mod volumes are roughly translated to attenuation levels for this register
		Effect C00 also sets the pan temporarily to 0 in order to silence the sample. This may not happen
		when restarting the song loop though. Best to add a 800 effect at the loop point if you want to ensure
		the sample is muted after the song loop.
	
		Panpot has range of 15 possible values. Low nibble of 8xx effects is ignored.
		A value of 0 mutes the sample completely.
		
		TL;DR:
			Sample length slightly reduced for non-looping samples
			Sample offset loops may not work
			Pan range 0x10-0xF0, 0x00 mutes the sample, low nibble ignored.
			Volumes may be a little off.
		
	YMZ280B
		Considered for inclusion.
		
		Max 8 channels.
		Max sample length: 16m
		Max loop length: same
		Supports 8/16-bit PCM samples, and 4-bit ADPCM.
		
		Frequency range is limited, 0-88200 Hz in 512 steps for PCM, 0-44100 in 256 steps for ADPCM.
		This means 172 Hz per step, which might be noticable for low frequency samples...
		
		Would support sample offset though.
	
To do:
		Support all effects...
		More cleanups...
		Implement XM support...
		
