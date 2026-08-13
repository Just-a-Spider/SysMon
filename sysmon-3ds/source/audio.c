// audio.c — UI sound feedback via ndsp
// Synthesises two short tones entirely at runtime; no .wav files needed.
// Requires dspfirm.cdc on SD card (or built-in DSP init) and ndsp in LIBS.
//
// click   : 880 Hz square wave, 60 ms  — tab switch
// confirm : 440 Hz square wave, 80 ms  — macro / action send

#include "audio.h"
#include <3ds.h>
#include <string.h>
#include <stdlib.h>

// DSP audio config
#define SAMPLE_RATE   22050
#define NUM_CHANNELS  1

// Tone buffers (mono s16)
#define CLICK_SAMPLES   ((SAMPLE_RATE * 60) / 1000)   // 60 ms = 1323 samples
#define CONFIRM_SAMPLES ((SAMPLE_RATE * 80) / 1000)   // 80 ms = 1764 samples

static s16 *click_buf   = NULL;
static s16 *confirm_buf = NULL;

static ndspWaveBuf click_wb;
static ndspWaveBuf confirm_wb;

static int audio_ok = 0; // set to 1 only if ndspInit succeeded

// Build a square wave tone into a linearAlloc'd buffer.
// freq_hz controls pitch; amplitude is fixed at ~40% to avoid harshness.
static s16 *make_tone(int num_samples, float freq_hz)
{
    s16 *buf = (s16 *)linearAlloc(num_samples * sizeof(s16));
    if (!buf)
        return NULL;

    // Samples per half-period
    float half_period = (float)SAMPLE_RATE / (freq_hz * 2.0f);
    for (int i = 0; i < num_samples; i++)
    {
        int phase = (int)(i / half_period) & 1;
        buf[i] = phase ? 12000 : -12000;
    }
    return buf;
}

void audio_init(void)
{
    if (ndspInit() != 0)
        return; // DSP firmware not available — silent degradation

    ndspSetOutputMode(NDSP_OUTPUT_MONO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, (float)SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);

    float mix[12] = {0};
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    click_buf   = make_tone(CLICK_SAMPLES,   880.0f);
    confirm_buf = make_tone(CONFIRM_SAMPLES, 440.0f);

    if (!click_buf || !confirm_buf)
    {
        ndspExit();
        return;
    }

    DSP_FlushDataCache(click_buf,   CLICK_SAMPLES   * sizeof(s16));
    DSP_FlushDataCache(confirm_buf, CONFIRM_SAMPLES * sizeof(s16));

    audio_ok = 1;
}

void audio_exit(void)
{
    if (!audio_ok)
        return;
    ndspExit();
    if (click_buf)   { linearFree(click_buf);   click_buf   = NULL; }
    if (confirm_buf) { linearFree(confirm_buf); confirm_buf = NULL; }
    audio_ok = 0;
}

// Play a tone on channel 0. Previous tone is interrupted — this is intentional
// for quick repeat taps; we don't want sounds stacking.
static void play_buf(s16 *buf, int num_samples, ndspWaveBuf *wb)
{
    if (!audio_ok || !buf)
        return;

    ndspChnWaveBufClear(0);
    memset(wb, 0, sizeof(ndspWaveBuf));
    wb->data_vaddr = buf;
    wb->nsamples   = num_samples;
    wb->looping    = false;
    ndspChnWaveBufAdd(0, wb);
}

void audio_play_click(void)
{
    play_buf(click_buf, CLICK_SAMPLES, &click_wb);
}

void audio_play_confirm(void)
{
    play_buf(confirm_buf, CONFIRM_SAMPLES, &confirm_wb);
}
