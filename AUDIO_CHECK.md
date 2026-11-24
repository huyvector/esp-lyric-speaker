# Audio checks

Use a regulated 5 V supply and common ground. Begin at modest volume with Bass Boost off, then choose the desired level. This firmware downmixes stereo to mono before sending it to both I2S channels. Keep only one speaker connected to its own amplifier channel; never connect an amplifier negative output to ground or join two amplifier outputs.

If audio breaks up, compare USB logs on both boards with the supply voltage under load. Brownout, dropouts, clipping and mechanical speaker limits need separate diagnosis. Avoid excessive bass boost on a small driver. The WeAct amplified board and a bare PCM5102A line-out board have different output wiring.
