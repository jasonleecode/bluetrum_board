/* libbtctrl.a:platform_asm.o contains only this absolute symbol.
 * This is the ROM scheduler environment address, not newly allocated RAM. */
__asm__(".global nanos_env\n.set nanos_env,0x104e4\n");
