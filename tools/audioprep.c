// Crops / converts a sound file for the game: 44.1 kHz 16-bit mono WAV,
// peak-normalised, with short fades.
//   audioprep in.wav out.wav start_s length_s [peak]
//   audioprep in.wav --envelope       (prints a loudness envelope to pick cuts)
// Build: gcc tools/audioprep.c -Ilib/raylib-6.0_win64_mingw-w64/include
//        -Llib/raylib-6.0_win64_mingw-w64/lib -lraylib -lopengl32 -lgdi32 -lwinmm -o audioprep.exe
#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
	SetTraceLogLevel(LOG_WARNING);
	if (argc < 3) { printf("usage: audioprep in out start len [peak] | audioprep in --envelope\n"); return 1; }
	Wave w = LoadWave(argv[1]);
	if (!w.data) { printf("cannot load %s\n", argv[1]); return 1; }
	WaveFormat(&w, 44100, 32, 1);
	float* s = (float*)w.data;
	int n = (int)w.frameCount;
	if (strcmp(argv[2], "--envelope") == 0) {
		int win = 4410;
		for (int i = 0; i + win <= n; i += win) {
			double acc = 0;
			for (int k = 0; k < win; ++k) acc += s[i + k] * s[i + k];
			double db = 10 * log10(acc / win + 1e-12);
			int bars = (int)((db + 60) / 2); if (bars < 0) bars = 0;
			printf("%6.1fs %6.1f dB ", i / 44100.0, db);
			for (int b = 0; b < bars; ++b) putchar('#');
			putchar('\n');
		}
		return 0;
	}
	if (argc < 5) { printf("need start and length\n"); return 1; }
	int a = (int)(atof(argv[3]) * 44100), len = (int)(atof(argv[4]) * 44100);
	float peak = argc > 5 ? (float)atof(argv[5]) : 0.89f;
	if (a < 0) a = 0;
	if (a + len > n) len = n - a;
	if (len <= 0) { printf("empty range\n"); return 1; }
	short* out = (short*)malloc(sizeof(short) * len);
	float pk = 1e-6f;
	for (int i = 0; i < len; ++i) if (fabsf(s[a + i]) > pk) pk = fabsf(s[a + i]);
	float g = peak / pk;
	int fin = 220, fout = len / 8 < 4410 ? len / 8 : 4410;
	for (int i = 0; i < len; ++i) {
		float v = s[a + i] * g;
		if (i < fin) v *= (float)i / fin;
		if (i > len - fout) v *= (float)(len - i) / fout;
		if (v > 1) v = 1; if (v < -1) v = -1;
		out[i] = (short)(v * 32767);
	}
	Wave o = { (unsigned)len, 44100, 16, 1, out };
	if (!ExportWave(o, argv[2])) { printf("export failed\n"); return 1; }
	printf("%s: %.2fs (gain %.1fx)\n", argv[2], len / 44100.0, g);
	return 0;
}
