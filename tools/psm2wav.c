/*
 * OpenJazz CD32X - the game's music (.PSM, played by the libxmp bundled with OpenJazz) as WAV
 * files for CD audio (J10b). 44.1 kHz, 16-bit, stereo; one pass through the song, at most
 * MAX_SECONDS (the console repeats a CD track by itself).
 *   psm2wav <in.PSM> <out.wav> [max seconds]
 */
#include "xmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: psm2wav in.PSM out.wav [max seconds]\n"); return 2; }
	const int max_s = argc > 3 ? atoi(argv[3]) : 120;
	xmp_context c = xmp_create_context();
	if (xmp_load_module(c, argv[1]) != 0) { fprintf(stderr, "%s: cannot load\n", argv[1]); return 1; }
	if (xmp_start_player(c, 44100, 0) != 0) { fprintf(stderr, "%s: cannot play\n", argv[1]); return 1; }
	FILE *f = fopen(argv[2], "wb");
	if (!f) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
	fwrite("data\0\0\0\0", 1, 8, f);
	static int16_t buf[4096];
	uint32_t bytes = 0, max_bytes = (uint32_t)max_s * 44100u * 4u;
	while (bytes < max_bytes && xmp_play_buffer(c, buf, sizeof buf, 1) == 0) {
		fwrite(buf, 1, sizeof buf, f);   /* little-endian host: WAV byte order */
		bytes += sizeof buf;
	}
	fseek(f, 4, SEEK_SET); put32(f, 36 + bytes);
	fseek(f, 40, SEEK_SET); put32(f, bytes);
	fclose(f);
	xmp_end_player(c);
	xmp_release_module(c);
	xmp_free_context(c);
	printf("  %-14s %3u s\n", argv[2], bytes / (44100u * 4u));
	return 0;
}
