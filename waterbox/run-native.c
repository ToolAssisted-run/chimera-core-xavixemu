/* run-native.c - the native reference for the core tests.
 *
 * Links the SAME cinterface.c and XaviXEmu objects the guest build uses
 * (emulibc degraded to malloc by native-shim/) and drives the exports
 * directly. The work dir holds the same files the sandbox would see mounted:
 * the game under its real name, an optional save data file, and a "slots"
 * JSON naming them.
 *
 * usage: run-native <workdir> [--frames N] [--exercise] [--press B:F:N]
 *        [--screenshot out.tga] [--dump-domain D out]
 *        [--savedata-out dir] [--turbo] [--shot F:out.tga]
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gate-harness.h"

/* what the shim's clock_start_seconds() answers (native-shim/emulibc.h) */
int64_t chimera_native_clock_start;

extern int Init(void);
extern const char *GetLoadError(void);
extern void SetButton(int32_t index, int32_t state);
extern void FrameAdvance(uint64_t packed);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int InputWasRead(void);
extern uint64_t XavixStateHash(void);
extern void SetRenderingEnabled(int on);
extern int GetVsyncNumerator(void);
extern int GetVsyncDenominator(void);
extern int GetMemoryDomainCount(void);
extern const char *GetMemoryDomainName(int i);
extern uint8_t *GetMemoryDomainPtr(int i);
extern int64_t GetMemoryDomainSize(int i);
extern int32_t GetSaveDataFileCount(void);
extern const char *GetSaveDataFileName(int32_t i);
extern int64_t GetSaveDataFileSize(int32_t i);
extern const uint8_t *GetSaveDataFileBuffer(int32_t i);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}
static const int16_t *audio(int *n)
{
	*n = GetAudioSampleCount();
	return GetAudio();
}
static const uint8_t *domain_ptr(int i) { return GetMemoryDomainPtr(i); }

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options]\n");
		return 2;
	}
	struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 2, &o))
		return 2;
	chimera_native_clock_start = o.clockStart;
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}

	struct gate_core c = {
		.init = Init,
		.load_error = GetLoadError,
		.set_button = SetButton,
		.frame = frame,
		.video = video,
		.audio = audio,
		.input_was_read = InputWasRead,
		.domain_count = GetMemoryDomainCount,
		.domain_name = GetMemoryDomainName,
		.domain_ptr = domain_ptr,
		.domain_size = GetMemoryDomainSize,
		.vsync_numerator = GetVsyncNumerator,
		.vsync_denominator = GetVsyncDenominator,
		.savedata_count = GetSaveDataFileCount,
		.savedata_name = GetSaveDataFileName,
		.savedata_size = GetSaveDataFileSize,
		.savedata_buffer = GetSaveDataFileBuffer,
		.state_hash = XavixStateHash,
		.pre_frame = NULL,
		.set_rendering = SetRenderingEnabled,
	};
	return gate_run(&c, &o);
}
