/* gate-harness.h - the shared half of run-native.c and run-wbx.c.
 *
 * One run-and-digest loop over an abstract core interface, so the native
 * reference and the sandboxed core run EXACTLY the same schedule: the same
 * presses, digesting the picture, the sound, lag and every memory domain per
 * frame. The two drivers differ only in how the exports are reached.
 *
 * The controls (waterbox.config "input.buttons" order): 0 Up, 1 Down,
 * 2 Left, 3 Right, 4 OK, 5 Back, 6 Pause.
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATE_BTN_COUNT 7
#define GATE_MAX_PRESSES 128
#define GATE_MAX_SHOTS 64

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	int (*vsync_numerator)(void);
	int (*vsync_denominator)(void);
	int32_t (*savedata_count)(void);
	const char *(*savedata_name)(int32_t i);
	int64_t (*savedata_size)(int32_t i);
	const uint8_t *(*savedata_buffer)(int32_t i);
	/* optional: a hash of everything the machine holds (the core's own state image) */
	uint64_t (*state_hash)(void);
	/* optional per-frame hook (the rerecord test); may be NULL */
	void (*pre_frame)(void);
	/* optional: turn the core's drawing on and off (the turbo test); may be NULL */
	void (*set_rendering)(int on);
};

struct gate_press { int button; long from, count; };
struct gate_shot { long frame; const char *path; };

struct gate_opts
{
	long frames;
	const char *screenshotPath; /* optional final-frame .tga */
	int exercise;               /* nonzero: drive the buttons with a deterministic pattern */
	const char *dumpDomain;     /* optional: memory domain to dump after the run... */
	const char *dumpPath;       /* ...into this file */
	const char *savedataDir;    /* optional: write every savedata export here after the run */
	int turbo;                  /* nonzero: draw nothing for the first half of the run */
	long long clockStart;       /* the second the machine's clock starts at */
	int pressCount;
	struct gate_press presses[GATE_MAX_PRESSES];
	int shotCount;              /* optional: pictures of chosen frames, as .tga */
	struct gate_shot shots[GATE_MAX_SHOTS];
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

/* A deterministic pattern, the same in both drivers, so the input path is
 * part of the comparison. It holds each combination for 8 frames, as a
 * person holds a button. */
static void gate_exercise(long frame, uint8_t *buttons)
{
	uint64_t x = (uint64_t)(frame / 8) * 6364136223846793005ULL + 1442695040888963407ULL;
	x ^= x >> 33;
	for (int k = 0; k < GATE_BTN_COUNT; k++)
		buttons[k] = (x >> (k * 3)) & 1;
}

static int gate_write_tga(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	uint8_t hdr[18] = { 0 };
	hdr[2] = 2;
	hdr[12] = w & 0xff; hdr[13] = (w >> 8) & 0xff;
	hdr[14] = h & 0xff; hdr[15] = (h >> 8) & 0xff;
	hdr[16] = 32;
	hdr[17] = 0x20;
	fwrite(hdr, 1, 18, f);
	fwrite(bgra, 4, (size_t)w * h, f);
	fclose(f);
	return 1;
}

/* the loop both drivers share; prints the digest block to stdout */
static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error ? c->load_error() : "?");
		return 1;
	}

	const long frames = o->frames;
	uint64_t vh = 0, ah = 0, th = 0, sh = 0;
	/* the second half of the run, hashed separately: see the turbo test */
	const long tail = frames / 2;
	long lag = 0, samples = 0, loud = 0;
	uint8_t buttons[GATE_BTN_COUNT];
	uint8_t prev[GATE_BTN_COUNT];
	memset(prev, 0, sizeof prev);

	for (long f = 0; f < frames; f++)
	{
		memset(buttons, 0, sizeof buttons);
		if (o->exercise)
			gate_exercise(f, buttons);
		for (int p = 0; p < o->pressCount; p++)
		{
			const struct gate_press *pr = &o->presses[p];
			if (f >= pr->from && f < pr->from + pr->count)
				buttons[pr->button] = 1;
		}

		if (o->turbo && c->set_rendering)
			c->set_rendering(f >= tail);

		if (c->pre_frame)
			c->pre_frame();

		for (int i = 0; i < GATE_BTN_COUNT; i++)
		{
			if (buttons[i] != prev[i])
				c->set_button(i, buttons[i]);
			prev[i] = buttons[i];
		}

		c->frame();

		int w = 0, h = 0, n = 0;
		const uint32_t *video = c->video(&w, &h);
		const int16_t *audio = c->audio(&n);
		vh = gate_fnv(vh, &w, sizeof w);
		vh = gate_fnv(vh, &h, sizeof h);
		vh = gate_fnv(vh, video, (size_t)w * h * 4);
		if (f >= tail)
		{
			th = gate_fnv(th, &w, sizeof w);
			th = gate_fnv(th, &h, sizeof h);
			th = gate_fnv(th, video, (size_t)w * h * 4);
		}
		ah = gate_fnv(ah, &n, sizeof n);
		ah = gate_fnv(ah, audio, (size_t)n * 2 * sizeof(int16_t));
		samples += n;
		for (int k = 0; k < n * 2; k++)
			loud += audio[k] != 0;
		if (!c->input_was_read())
			lag++;
		if (c->state_hash && (f + 1) % 60 == 0)
		{
			const uint64_t v = c->state_hash();
			sh = gate_fnv(sh, &v, sizeof v);
		}
		if (o->screenshotPath && f == frames - 1)
			gate_write_tga(o->screenshotPath, video, w, h);
		for (int k = 0; k < o->shotCount; k++)
			if (o->shots[k].frame == f)
				gate_write_tga(o->shots[k].path, video, w, h);
	}

	printf("frames=%ld\n", frames);
	printf("vsync=%d/%d\n", c->vsync_numerator(), c->vsync_denominator());
	printf("videoHash=%016llx\n", (unsigned long long)vh);
	printf("tailVideoHash=%016llx\n", (unsigned long long)th);
	printf("audioHash=%016llx\n", (unsigned long long)ah);
	printf("audioSamples=%ld\n", samples);
	printf("audioNonZero=%ld\n", loud);
	printf("lagFrames=%ld\n", lag);
	if (c->state_hash)
	{
		printf("stateHashes=%016llx\n", (unsigned long long)sh);
		printf("stateHash=%016llx\n", (unsigned long long)c->state_hash());
	}
	int nd = c->domain_count();
	for (int i = 0; i < nd; i++)
	{
		uint64_t dh = gate_fnv(0, c->domain_ptr(i), (size_t)c->domain_size(i));
		printf("domain[%s]=%016llx\n", c->domain_name(i), (unsigned long long)dh);
	}
	if (o->savedataDir)
	{
		int32_t count = c->savedata_count();
		printf("savedataFiles=%d\n", count);
		for (int32_t i = 0; i < count; i++)
		{
			char path[4096];
			snprintf(path, sizeof path, "%s/%s", o->savedataDir, c->savedata_name(i));
			FILE *f = fopen(path, "wb");
			if (!f) { perror(path); return 1; }
			fwrite(c->savedata_buffer(i), 1, (size_t)c->savedata_size(i), f);
			fclose(f);
		}
	}

	if (o->dumpDomain && o->dumpPath)
	{
		int found = 0;
		for (int i = 0; i < nd; i++)
		{
			if (strcmp(c->domain_name(i), o->dumpDomain) != 0)
				continue;
			FILE *f = fopen(o->dumpPath, "wb");
			if (!f) { perror(o->dumpPath); return 1; }
			fwrite(c->domain_ptr(i), 1, (size_t)c->domain_size(i), f);
			fclose(f);
			found = 1;
			break;
		}
		if (!found)
		{
			fprintf(stderr, "no such domain to dump: %s\n", o->dumpDomain);
			return 1;
		}
	}
	return 0;
}

/* shared command line: 0 on an error */
static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 600;
	o->clockStart = 1495889068LL; /* the sandbox's own when it is told none */
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) o->screenshotPath = argv[++i];
		else if (!strcmp(argv[i], "--exercise")) o->exercise = 1;
		else if (!strcmp(argv[i], "--dump-domain") && i + 2 < argc) { o->dumpDomain = argv[++i]; o->dumpPath = argv[++i]; }
		else if (!strcmp(argv[i], "--savedata-out") && i + 1 < argc) o->savedataDir = argv[++i];
		else if (!strcmp(argv[i], "--turbo")) o->turbo = 1;
		else if (!strcmp(argv[i], "--clock-start") && i + 1 < argc) o->clockStart = strtoll(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--press") && i + 1 < argc)
		{
			/* --press <button>:<first frame>:<frames held> */
			struct gate_press pr;
			if (o->pressCount >= GATE_MAX_PRESSES
				|| sscanf(argv[++i], "%d:%ld:%ld", &pr.button, &pr.from, &pr.count) != 3
				|| pr.button < 0 || pr.button >= GATE_BTN_COUNT)
			{
				fprintf(stderr, "bad --press %s (want <button 0-6>:<from>:<count>)\n", argv[i]);
				return 0;
			}
			o->presses[o->pressCount++] = pr;
		}
		else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
		{
			/* --shot <frame>:<file.tga>: the picture of that frame (counted from 0) */
			char *colon = strchr(argv[++i], ':');
			if (o->shotCount >= GATE_MAX_SHOTS || !colon || colon == argv[i])
			{
				fprintf(stderr, "bad --shot %s (want <frame>:<file.tga>)\n", argv[i]);
				return 0;
			}
			o->shots[o->shotCount].frame = strtol(argv[i], 0, 0);
			o->shots[o->shotCount].path = colon + 1;
			o->shotCount++;
		}
		else if (!strcmp(argv[i], "--rerecord") || !strcmp(argv[i], "--session")) ; /* run-wbx's; ignored here */
		else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 0; }
	}
	return 1;
}

#endif /* GATE_HARNESS_H */
