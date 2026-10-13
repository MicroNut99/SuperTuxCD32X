What matters most for SuperTux:

COMM6 is free. The 68000’s write to it was already removed (hw_md.s line 1307, OpenLara’s fix). COMM4 is only used when the second SH2 starts up. So nothing in our boot path clashes with the tiler.
Drawing on both SH2s gave no gain in OpenJazz: both waited on the same bus. The tiler starts on one CPU. What did help, in Hurrican, was running the game rules on the second SH2 while the first one draws.
The 0xFF line bug on real hardware. Hurrican’s screen layout can hit it, because it uses the same 384-byte line width as Vic’s tiler. A line can show wrongly when the camera’s sideways offset is 127, 255 or 383. That doesn’t change the boot path: SuperTux’s camera will just step one pixel past those three values, and the PC build will check every frame for it. I couldn’t find any record of Hurrican seeing this on the console, so it may be rare.
Don’t let the tiler set up the screen itself. Sonic32 lets the tiler set up the 32X screen, but here the platform layer owns that. The tiler will draw into Hurrican’s screen area and show frames through Hurrican’s usual call.
Background pictures cost a lot on the 32X. In Hurrican, three full background layers took 85 ms a frame. Moving them to the Genesis planes means changing the boot stack, so like Hurrican and Tyrian, that waits until the very end.
Some smaller rules are also noted:
Keep colour 0 black.
Write the frame buffer in 16-bit steps only.
Never read the CD while music plays.
Don’t let the tiler draw sprites narrower than 4 units: they overwrite memory.
The clock is the vblank count plus the watchdog.
Never malloc on the second SH2.