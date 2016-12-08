welcome to the FM experimental branch...

how to use:

to add an FM instrument:

put "FM: " in the instrument name followed by a base64 string of the FM patch data
FM data is stored just like http://www.shikadi.net/moddingwiki/S3M_Format#Adlib (see the oplValues)

example: "FM: EAEAgBp3EFgCAA4="

You can use fine tune and volume values.

To enable FM on a channel, use command EFx, where x is the channel number (1-14, sorry if you want more channels)
to key off, you can use command EFF or ECx

have fun...
