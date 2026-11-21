/* cinterface.c - XaviXEmu (XaviX games on a 6502-family machine) behind
 * Chimera's guest ABI.
 *
 * XaviXEmu's emulation sources (src/core, sha1.c and the miniz library) are a
 * set of plain C files with no operating-system calls: a game's ROM goes in,
 * a controller byte is set, one frame is run, and a picture and the sound come
 * out. Upstream's Windows window (main.c and its helpers) is not used. This
 * file is the frontend: it finds the game's ROM in what the project gives it,
 * checks it against the table of ROM sets XaviXEmu knows (games.inc), and
 * drives the machine.
 *
 * This file compiles identically for the guest (miniBox's emulibc) and for
 * the native reference build (native-shim/emulibc.h), which is what makes
 * the comparison of the two a proof.
 *
 * Only games with plain controls are played: a pad of four directions and two
 * buttons that the machine reads as bits of one register. Games that need a
 * camera, a motion sensor, an analog stick, a wheel, a keyboard or a card
 * scanner are recognised and refused with a message that says so (docs/PLAN.md).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_slots.h>

#include "drgqst_core.h"
#include "drgqst_state.h"
#include "sha1.h"
#include "miniz.h"

/* this core's windows in Chimera's Emulator menu: miniBox's ready-made Export
 * Save Data and CPU Registers, built on its exports (Chimera has no tool of
 * its own for a core, Chimera issue 610) */
#define WBX_WINDOWS_IMPLEMENTATION
#include <waterbox_windows.h>

/* A text a person reads is a key of languages/en.json, each of its arguments
 * after a TEXT_ARG (chimera docs/translations.md). */
#define TEXT_ARG "\x1f"

/* The largest file the game slot takes. The biggest ROM is 8 MiB, and a ZIP of
 * it is smaller; anything much larger is not a game and is refused before it
 * is read. */
#define MAX_CONTENT (16u << 20)

/* ---- the table of ROM sets ----
 * What a ROM set is, and why it is or is not played here. */
enum reason
{
	REASON_NONE = 0, /* played: plain controls */
	REASON_MOTION,   /* a camera or motion sensor sees the player */
	REASON_ANALOG,   /* analog sticks or a wheel */
	REASON_KEYBOARD, /* a keyboard and mouse */
	REASON_SCANNER,  /* a card scanner */
	REASON_XAVIX2    /* the XaviX 2 machine, which this core does not run */
};

static const char *const k_reasonKey[] = {
	"", "reason.motion", "reason.analog", "reason.keyboard", "reason.scanner", "reason.xavix2"
};

typedef struct game
{
	const char *shortName;
	const char *title;
	uint32_t size;
	uint32_t crc32;
	uint8_t sha1[20];
	enum reason reason;
	int profile; /* enum drgqst_core_profile; 0 for a game that is refused */
} game;

static const game k_games[] = {
#include "games.inc"
};
#define GAME_COUNT (sizeof k_games / sizeof k_games[0])

/* The two-chip set of Ham-chans (epo_hamd) has no single image: its ZIP holds
 * a 1 MiB and a 2 MiB chip, which XaviXEmu joins. Its controls are wireless
 * packets sent from two shaken gloves, so it is only recognised, to be
 * refused by name. */
#define HAMD_U2_SIZE 0x100000u
#define HAMD_U2_CRC32 0x6c2d9d98u
#define HAMD_TITLE "Ham-chans: Ham Ham Challenge! Atsumare Ham-chans!"

/* ---- the controls: waterbox.config "input.buttons" order ----
 * 0 Up, 1 Down, 2 Left, 3 Right, 4 OK, 5 Back, 6 Pause
 * The machine reads them as one byte, a bit set for a pressed button. */
#define BTN_COUNT 7
static const uint8_t k_inputBit[BTN_COUNT] = { 0x10, 0x20, 0x40, 0x80, 0x01, 0x02, 0x04 };
#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_LEFT 2
#define BTN_RIGHT 3
#define BTN_PAUSE 6

/* Two channels reach a core: the packed mask in FrameAdvance (the frontend)
 * and SetButton (the test harness). A frame is their union. */
static uint8_t g_setButtons[BTN_COUNT];

static char g_loadError[1200];
static drgqst_core g_core;
static uint8_t *g_rom;
static size_t g_romSize;
static const game *g_game;
static int g_inited;

/* The machine has no calendar clock and reads no time from the host. */

/* ---- lag frames and Chimera's input-poll callback ----
 * Patched into XaviXEmu (patches/0001): a call each time the program reads
 * the button register. */
static int g_inputRead;
ECL_INVISIBLE static uintptr_t (*g_inputPoll)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

ECL_EXPORT void SetInputPollCallback(uint64_t fn)
{
	g_inputPoll = (uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)fn;
}

static void on_input_read(void)
{
	g_inputRead = 1;
	if (g_inputPoll)
		g_inputPoll(0, 0, 0, 0, 0, 0);
}

/* Turbo: the frontend's choice for the moment, not the machine's (patches/0002). */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

/* ---- loading ---- */
static uint8_t *read_file(const char *name, size_t maxSize, long *outSize)
{
	*outSize = 0;
	FILE *f = fopen(name, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	*outSize = size;
	if (size <= 0 || (size_t)size > maxSize)
	{
		fclose(f);
		return NULL;
	}
	uint8_t *buf = (uint8_t *)malloc((size_t)size);
	if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size)
	{
		free(buf);
		fclose(f);
		*outSize = -1;
		return NULL;
	}
	fclose(f);
	return buf;
}

static const game *find_game(size_t size, uint32_t crc)
{
	for (size_t i = 0; i < GAME_COUNT; i++)
		if (k_games[i].size == size && k_games[i].crc32 == crc)
			return &k_games[i];
	return NULL;
}

static int sha1_ok(const game *g, const uint8_t *data)
{
	uint8_t digest[20];
	sha1_calculate(data, g->size, digest);
	return memcmp(digest, g->sha1, 20) == 0;
}

/* The ROM image in a file the project gave: a ZIP of ROM chips as MAME names
 * them, or the bare image. Returns the game, with its image in g_rom, or NULL
 * with g_loadError set. */
static const game *identify(const char *name, uint8_t *data, size_t size)
{
	const game *found = NULL;
	int hamd = 0;
	if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0)
	{
		mz_zip_archive zip;
		memset(&zip, 0, sizeof zip);
		if (!mz_zip_reader_init_mem(&zip, data, size, 0))
		{
			snprintf(g_loadError, sizeof g_loadError, "error.bad_zip" TEXT_ARG "%s", name);
			return NULL;
		}
		const mz_uint count = mz_zip_reader_get_num_files(&zip);
		for (mz_uint i = 0; i < count && !found; i++)
		{
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
				continue;
			if (st.m_uncomp_size == HAMD_U2_SIZE && st.m_crc32 == HAMD_U2_CRC32)
				hamd = 1;
			const game *g = find_game((size_t)st.m_uncomp_size, (uint32_t)st.m_crc32);
			if (!g)
				continue;
			if (g->reason != REASON_NONE)
			{
				found = g; /* refused by name; no need to extract it */
				break;
			}
			uint8_t *rom = (uint8_t *)malloc(g->size);
			if (!rom || !mz_zip_reader_extract_to_mem(&zip, i, rom, g->size, 0))
			{
				free(rom);
				mz_zip_reader_end(&zip);
				snprintf(g_loadError, sizeof g_loadError, "error.bad_zip" TEXT_ARG "%s", name);
				return NULL;
			}
			g_rom = rom;
			g_romSize = g->size;
			found = g;
		}
		mz_zip_reader_end(&zip);
	}
	else
	{
		const uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, data, size);
		found = find_game(size, crc);
		if (found && found->reason == REASON_NONE)
		{
			g_rom = (uint8_t *)malloc(size);
			if (!g_rom)
			{
				snprintf(g_loadError, sizeof g_loadError, "error.no_machine");
				return NULL;
			}
			memcpy(g_rom, data, size);
			g_romSize = size;
		}
	}
	if (!found && hamd)
	{
		snprintf(g_loadError, sizeof g_loadError, "error.not_plain" TEXT_ARG "%s" TEXT_ARG "%s", HAMD_TITLE,
			k_reasonKey[REASON_MOTION]);
		return NULL;
	}
	if (!found)
	{
		snprintf(g_loadError, sizeof g_loadError, "error.unknown_game" TEXT_ARG "%s", name);
		return NULL;
	}
	if (found->reason != REASON_NONE)
	{
		snprintf(g_loadError, sizeof g_loadError, "error.not_plain" TEXT_ARG "%s" TEXT_ARG "%s", found->title,
			k_reasonKey[found->reason]);
		return NULL;
	}
	/* an image with the right size and CRC-32 but another SHA-1 is a different image */
	if (!sha1_ok(found, g_rom))
	{
		snprintf(g_loadError, sizeof g_loadError, "error.bad_image" TEXT_ARG "%s" TEXT_ARG "%s", name,
			found->shortName);
		free(g_rom);
		g_rom = NULL;
		return NULL;
	}
	return found;
}

/* ---- save data ----
 * The machine remembers in two ways, by game: a 4 KiB battery-backed RAM
 * inside its main RAM (Excite Boxing) or a serial EEPROM of 1 KiB. A
 * savestate carries either; the file is the way out of a project and back
 * in. It is named after the game so that one game's file is never taken for
 * another's. */
static int uses_nvram(void) { return g_game->profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM; }
static size_t save_size(void) { return uses_nvram() ? XAVIX_PARALLEL_NVRAM_SIZE : XAVIX_EEPROM24C08_SIZE; }
static uint8_t *save_buffer(void)
{
	return uses_nvram() ? g_core.machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE
	                    : g_core.machine.state.peripherals.eeprom.data;
}
static void save_name(char *out, size_t cap) { snprintf(out, cap, "%s.sav", g_game->shortName); }

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';

	/* the game: the project slot's file, else the plain rom mount */
	char gameName[512];
	const char *name = "rom";
	if (wbx_slot_count("game") > 0 && wbx_slot_name("game", 0, gameName, sizeof gameName) != NULL)
		name = gameName;

	long size = 0;
	uint8_t *data = read_file(name, MAX_CONTENT, &size);
	if (!data)
	{
		if (size == 0 && name == gameName)
			snprintf(g_loadError, sizeof g_loadError, "error.not_read" TEXT_ARG "%s", name);
		else if (size == 0)
			snprintf(g_loadError, sizeof g_loadError, "error.no_game");
		else
			snprintf(g_loadError, sizeof g_loadError, "error.too_large" TEXT_ARG "%s", name);
		return 0;
	}
	g_game = identify(name, data, (size_t)size);
	free(data);
	if (!g_game)
		return 0;

	if (!drgqst_core_init_profile(&g_core, g_rom, g_romSize, (enum drgqst_core_profile)g_game->profile))
	{
		snprintf(g_loadError, sizeof g_loadError, "error.no_machine");
		return 0;
	}

	/* save data is taken back only under the name it is exported as: a file
	 * of another name would be carried by the project and then ignored */
	char save[512];
	if (wbx_slot_count("savedata") > 0 && wbx_slot_name("savedata", 0, save, sizeof save) != NULL)
	{
		char want[64];
		save_name(want, sizeof want);
		if (strcmp(save, want) != 0)
		{
			snprintf(g_loadError, sizeof g_loadError, "error.save_name" TEXT_ARG "%s" TEXT_ARG "%s", save, want);
			return 0;
		}
		uint8_t *saved = read_file(save, 1u << 20, &size);
		if (!saved || (size_t)size != save_size())
		{
			snprintf(g_loadError, sizeof g_loadError, "error.save_size" TEXT_ARG "%s" TEXT_ARG "%ld" TEXT_ARG "%u",
				save, size, (unsigned)save_size());
			free(saved);
			return 0;
		}
		if (uses_nvram())
			memcpy(save_buffer(), saved, save_size());
		else
			xavix_eeprom_load_image(&g_core.machine.state.peripherals.eeprom, saved, save_size());
		free(saved);
	}

	xavix_machine_input0_read_hook = on_input_read;
	g_inited = 1;
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < BTN_COUNT)
		g_setButtons[index] = state ? 1 : 0;
}

/* The machine has buttons only. The export exists for the harness, which
 * drives the whole ABI. */
ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
	(void)index;
	(void)value;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	uint8_t held[BTN_COUNT];
	for (int i = 0; i < BTN_COUNT; i++)
		held[i] = g_setButtons[i] | (uint8_t)((packed >> i) & 1);
	g_inputRead = 0;
	if (!g_inited)
		return;

	/* The controller's directions are switches in a ring: two opposite ones
	 * cannot be closed together, as XaviXEmu's own front end has it. */
	if (held[BTN_UP] && held[BTN_DOWN])
		held[BTN_UP] = held[BTN_DOWN] = 0;
	if (held[BTN_LEFT] && held[BTN_RIGHT])
		held[BTN_LEFT] = held[BTN_RIGHT] = 0;

	uint8_t input = 0;
	for (int i = 0; i < BTN_COUNT; i++)
		if (held[i])
			input |= k_inputBit[i];
	/* the Pause bit is wired in the racing and tilt games only; Excite Boxing
	 * leaves it unconnected, as XaviXEmu's front end does */
	if (uses_nvram())
		input &= (uint8_t)~k_inputBit[BTN_PAUSE];
	g_core.machine.state.input0 = input;

	drgqst_core_run_frame(&g_core);
}

ECL_EXPORT uint32_t *GetVideoBgra(void) { return (uint32_t *)drgqst_core_framebuffer(&g_core); }
ECL_EXPORT int GetVideoWidth(void) { return XAVIX_VIDEO_WIDTH; }
ECL_EXPORT int GetVideoHeight(void) { return XAVIX_VIDEO_HEIGHT; }

/* 48000 Hz stereo, 800 sample pairs a frame */
ECL_EXPORT int16_t *GetAudio(void) { return (int16_t *)drgqst_core_frame_audio(&g_core); }
ECL_EXPORT int GetAudioSampleCount(void) { return g_inited ? DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME : 0; }

ECL_EXPORT int GetVsyncNumerator(void) { return XAVIX_FRAME_RATE; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }

/* ---- the test harness: the machine's own canonical state image ----
 * XaviXEmu writes its state as a fixed little-endian image (drgqst_state.h).
 * The tests hash it after every run, native and sandbox, so that the machine
 * is compared by everything it holds and not only by the domains below. The
 * buffer is invisible: what a test reads is no part of a savestate. */
ECL_INVISIBLE static uint8_t g_stateImage[1u << 16];

ECL_EXPORT uint64_t XavixStateHash(void)
{
	size_t written = 0;
	if (!g_inited || drgqst_state_serialized_size() > sizeof g_stateImage
		|| !drgqst_state_save(&g_core, g_stateImage, sizeof g_stateImage, &written))
		return 0;
	uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < written; i++)
	{
		h ^= g_stateImage[i];
		h *= 1099511628211ULL;
	}
	return h ^ (uint64_t)written;
}

/* ---- memory domains ----
 * Main RAM, the sprite fragment RAM, the sound RAM, and the ROM (read-only). */
#define DOMAIN_COUNT 4
static const char *const k_domainNames[DOMAIN_COUNT] = { "Main RAM", "Fragment RAM", "Sound RAM", "ROM" };

ECL_EXPORT int GetMemoryDomainCount(void) { return g_inited ? DOMAIN_COUNT : 0; }

ECL_EXPORT const char *GetMemoryDomainName(int i)
{
	return i >= 0 && i < DOMAIN_COUNT ? k_domainNames[i] : NULL;
}

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i)
{
	if (!g_inited)
		return NULL;
	switch (i)
	{
	case 0: return g_core.machine.state.main_ram;
	case 1: return g_core.machine.state.fragment_ram;
	case 2: return g_core.machine.state.sound_ram;
	case 3: return g_rom;
	default: return NULL;
	}
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i)
{
	switch (i)
	{
	case 0: return XAVIX_MAIN_RAM_SIZE;
	case 1: return XAVIX_FRAGMENT_RAM_SIZE;
	case 2: return XAVIX_SOUND_RAM_SIZE;
	case 3: return (int64_t)g_romSize;
	default: return 0;
	}
}

ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && i < 3; }

/* ---- registers: the 65C02-family CPU's ---- */
#define REG_COUNT 11
static const char *const k_regNames[REG_COUNT] = { "A", "X", "Y", "S", "P", "PC", "CodeBank", "DataBank", "J", "K", "L" };
static const int k_regBits[REG_COUNT] = { 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8 };

ECL_EXPORT int GetRegisterCount(void) { return REG_COUNT; }
ECL_EXPORT const char *GetRegisterName(int i) { return i >= 0 && i < REG_COUNT ? k_regNames[i] : NULL; }
ECL_EXPORT int GetRegisterBits(int i) { return i >= 0 && i < REG_COUNT ? k_regBits[i] : 0; }
ECL_EXPORT int64_t GetRegisterValue(int i)
{
	if (!g_inited)
		return 0;
	const xavix_cpu_t *c = &g_core.cpu;
	switch (i)
	{
	case 0: return c->a;
	case 1: return c->x;
	case 2: return c->y;
	case 3: return c->s;
	case 4: return c->p;
	case 5: return c->pc;
	case 6: return c->code_bank;
	case 7: return c->data_bank;
	case 8: return c->j;
	case 9: return c->k;
	case 10: return c->l;
	default: return 0;
	}
}
ECL_EXPORT const char *GetRegisterRoles(void) { return "pc PC\nsp S\nflags P\n"; }

/* ---- save data ---- */
static char g_saveName[64];

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return g_inited ? 1 : 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
	if (!g_inited || i != 0)
		return NULL;
	save_name(g_saveName, sizeof g_saveName);
	return g_saveName;
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return g_inited && i == 0 ? (int64_t)save_size() : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return g_inited && i == 0 ? save_buffer() : NULL; }
