#ifdef PORTABLE

#include <stdint.h>

#include "gba/gba.h"
#include "gba/m4a_internal.h"
#include "platform/cgb_audio.h"

#define CGB_CHANNEL_COUNT 4
#define CGB_OSCILLATOR_PEAK 24

struct HostCgbOscillator
{
    u32 phase;
    u64 noisePhase;
    u64 lengthClock;
    u16 lfsr;
    u16 lengthCounter;
    bool8 active;
    bool8 lengthEnabled;
    u64 sweepClock;
    u16 sweepFrequency;
    u8 sweepPeriod;
    u8 sweepShift;
    bool8 sweepDecrease;
    bool8 sweepOverflow;
};

static struct HostCgbOscillator sOscillators[CGB_CHANNEL_COUNT];

static s8 ClampPcmSample(s32 sample)
{
    if (sample > INT8_MAX)
        return INT8_MAX;
    if (sample < INT8_MIN)
        return INT8_MIN;
    return (s8)sample;
}

static u32 CgbMixScale(void)
{
    u32 level = REG_SOUNDCNT_H & 3;

    if (level > 2)
        level = 2;
    return 1u << level;
}

static u32 SquarePhaseStep(u16 frequency, u32 sampleRate)
{
    u32 period = 2048 - (frequency & 0x7FF);

    return (u32)(((u64)131072 << 32) / ((u64)period * sampleRate));
}

static u32 WavePhaseStep(u16 frequency, u32 sampleRate)
{
    u32 period = 2048 - (frequency & 0x7FF);

    return (u32)(((u64)65536 << 32) / ((u64)period * sampleRate));
}

static u64 NoisePhaseStep(u8 frequency, u32 sampleRate)
{
    u32 divisor = frequency & 7;
    u32 shift = frequency >> 4;

    if (divisor != 0)
        divisor *= 2;
    else
        divisor = 1;
    return ((u64)524288 << 32) / ((u64)divisor * (1u << shift) * sampleRate);
}

static void ResetLengthCounter(u32 channel, struct HostCgbOscillator *oscillator)
{
    u32 length;
    u32 limit;
    u8 control;

    switch (channel)
    {
    case 0:
        length = REG_NR11 & 0x3F;
        control = REG_NR14;
        limit = 64;
        break;
    case 1:
        length = REG_NR21 & 0x3F;
        control = REG_NR24;
        limit = 64;
        break;
    case 2:
        length = REG_NR31;
        control = REG_NR34;
        limit = 256;
        break;
    default:
        length = REG_NR41 & 0x3F;
        control = REG_NR44;
        limit = 64;
        break;
    }

    oscillator->lengthClock = 0;
    oscillator->lengthCounter = limit - length;
    oscillator->lengthEnabled = (control & 0x40) != 0;
}

static bool8 AdvanceLengthCounter(struct HostCgbOscillator *oscillator, u32 sampleRate)
{
    if (!oscillator->lengthEnabled)
        return TRUE;

    oscillator->lengthClock += ((u64)256 << 32) / sampleRate;
    while (oscillator->lengthClock >= (1ull << 32))
    {
        oscillator->lengthClock -= 1ull << 32;
        if (oscillator->lengthCounter != 0 && --oscillator->lengthCounter == 0)
            return FALSE;
    }
    return oscillator->lengthCounter != 0;
}

static void ResetChannel1Sweep(struct HostCgbOscillator *oscillator)
{
    u8 sweep = REG_NR10;

    oscillator->sweepClock = 0;
    oscillator->sweepFrequency = REG_SOUND1CNT_X & 0x7FF;
    oscillator->sweepPeriod = (sweep >> 4) & 7;
    oscillator->sweepShift = sweep & 7;
    oscillator->sweepDecrease = (sweep & 8) != 0;
    oscillator->sweepOverflow = FALSE;
}

static bool8 AdvanceChannel1Sweep(struct HostCgbOscillator *oscillator, u32 sampleRate)
{
    u64 period;

    // The target writes NR10=8 when sweep is disabled. A zero period is also
    // treated as disabled, matching the hardware register semantics.
    if (oscillator->sweepPeriod == 0 || oscillator->sweepOverflow)
        return !oscillator->sweepOverflow;

    oscillator->sweepClock += ((u64)128 << 32) / sampleRate;
    period = (u64)oscillator->sweepPeriod << 32;
    while (oscillator->sweepClock >= period)
    {
        u32 delta;
        u32 nextFrequency;

        oscillator->sweepClock -= period;
        if (oscillator->sweepShift == 0)
            continue;

        delta = oscillator->sweepFrequency >> oscillator->sweepShift;
        if (oscillator->sweepDecrease)
        {
            nextFrequency = oscillator->sweepFrequency - delta;
        }
        else
        {
            nextFrequency = oscillator->sweepFrequency + delta;
            if (nextFrequency > 0x7FF)
            {
                oscillator->sweepOverflow = TRUE;
                return FALSE;
            }
        }

        oscillator->sweepFrequency = nextFrequency;
        REG_SOUND1CNT_X = (REG_SOUND1CNT_X & ~0x7FF) | nextFrequency;
    }

    return TRUE;
}

static s32 ChannelVolume(u32 channel)
{
    switch (channel)
    {
    case 0:
        return REG_NR12 >> 4;
    case 1:
        return REG_NR22 >> 4;
    case 2:
        switch (REG_NR32 & 0xE0)
        {
        case 0x20:
            return 15;
        case 0x40:
            return 8;
        case 0x60:
            return 4;
        case 0x80:
            return 12;
        default:
            return 0;
        }
    default:
        return REG_NR42 >> 4;
    }
}

static s32 SquareSample(u32 channel, u32 phase)
{
    static const u8 sDutySteps[] = {1, 2, 4, 6};
    u8 duty = channel == 0 ? REG_NR11 >> 6 : REG_NR21 >> 6;
    u8 step = phase >> 29;

    return step < sDutySteps[duty] ? 15 : -15;
}

static s32 WaveSample(u32 phase)
{
    const volatile u8 *waveRam = (const volatile u8 *)REG_ADDR_WAVE_RAM0;
    u32 position = phase >> 27;
    u8 packed = waveRam[position >> 1];
    u8 sample = position & 1 ? packed & 0xF : packed >> 4;

    return (s32)sample * 2 - 15;
}

static void ClockNoise(struct HostCgbOscillator *oscillator, bool8 narrow)
{
    u16 feedback = (oscillator->lfsr ^ (oscillator->lfsr >> 1)) & 1;

    oscillator->lfsr = (oscillator->lfsr >> 1) | (feedback << 14);
    if (narrow)
        oscillator->lfsr = (oscillator->lfsr & ~(1u << 6)) | (feedback << 6);
}

static s32 NoiseSample(struct HostCgbOscillator *oscillator, u64 phaseStep)
{
    s32 sample = oscillator->lfsr & 1 ? -15 : 15;
    u32 clocks;

    oscillator->noisePhase += phaseStep;
    clocks = oscillator->noisePhase >> 32;
    oscillator->noisePhase &= UINT32_MAX;
    while (clocks-- != 0)
        ClockNoise(oscillator, (REG_NR43 & 8) != 0);
    return sample;
}

static s32 ScaleSample(s32 sample, s32 volume, u32 mixScale, u32 outputLevel)
{
    return sample * volume * (s32)mixScale * (s32)outputLevel
         * CGB_OSCILLATOR_PEAK / (15 * 15 * 4 * 8);
}

static void MixChannel(u32 channel, struct HostCgbOscillator *oscillator,
                       s8 *right, s8 *left, u32 sampleCount, u32 sampleRate)
{
    u8 panBit = 1u << channel;
    u32 phaseStep = 0;
    u64 noiseStep = 0;
    u32 mixScale = CgbMixScale();
    u32 rightLevel = (REG_NR50 & 7) + 1;
    u32 leftLevel = ((REG_NR50 >> 4) & 7) + 1;
    s32 volume = ChannelVolume(channel);

    if (channel == 1)
        phaseStep = SquarePhaseStep(REG_SOUND2CNT_H, sampleRate);
    else if (channel == 2)
        phaseStep = WavePhaseStep(REG_SOUND3CNT_X, sampleRate);
    else if (channel == 3)
        noiseStep = NoisePhaseStep(REG_NR43, sampleRate);

    if (channel == 0 && !oscillator->sweepOverflow
     && (REG_SOUND1CNT_X & 0x7FF) != oscillator->sweepFrequency)
    {
        // CgbSound may update pitch between sweep ticks (bend/tune). Treat
        // that register write as the new sweep shadow frequency.
        oscillator->sweepFrequency = REG_SOUND1CNT_X & 0x7FF;
    }

    if (channel == 0 && oscillator->sweepOverflow)
        return;
    if (oscillator->lengthEnabled && oscillator->lengthCounter == 0)
        return;

    for (u32 i = 0; i < sampleCount; i++)
    {
        s32 sample;

        if (channel == 0)
        {
            if (!AdvanceChannel1Sweep(oscillator, sampleRate))
                return;
            // Software pitch writes change the live oscillator frequency, while
            // the sweep unit keeps its separately triggered shadow frequency.
            phaseStep = SquarePhaseStep(REG_SOUND1CNT_X, sampleRate);
        }

        if (channel < 2)
            sample = SquareSample(channel, oscillator->phase);
        else if (channel == 2)
            sample = WaveSample(oscillator->phase);
        else
            sample = NoiseSample(oscillator, noiseStep);

        if (REG_NR51 & panBit)
        {
            s32 mixed = ScaleSample(sample, volume, mixScale, rightLevel);
            right[i] = ClampPcmSample((s32)right[i] + mixed);
        }
        if (REG_NR51 & (panBit << 4))
        {
            s32 mixed = ScaleSample(sample, volume, mixScale, leftLevel);
            left[i] = ClampPcmSample((s32)left[i] + mixed);
        }
        oscillator->phase += phaseStep;
        if (!AdvanceLengthCounter(oscillator, sampleRate))
            return;
    }
}

void HostMixCgbAudio(struct SoundInfo *soundInfo)
{
    bool8 restart[CGB_CHANNEL_COUNT];
    u32 sampleCount;
    u32 block;
    u32 offset;
    s8 *right;
    s8 *left;

    if (soundInfo == NULL || soundInfo->cgbChans == NULL
     || (soundInfo->ident != ID_NUMBER && soundInfo->ident != ID_NUMBER + 1))
        return;

    for (u32 i = 0; i < CGB_CHANNEL_COUNT; i++)
        restart[i] = (soundInfo->cgbChans[i].statusFlags & SOUND_CHANNEL_SF_START) != 0;
    if (soundInfo->CgbSound != NULL)
        soundInfo->CgbSound();

    if (soundInfo->pcmSamplesPerVBlank <= 0
     || soundInfo->pcmSamplesPerVBlank > PCM_DMA_BUF_SIZE
     || soundInfo->pcmDmaPeriod == 0 || soundInfo->pcmFreq <= 0)
        return;

    sampleCount = (u32)soundInfo->pcmSamplesPerVBlank;
    block = soundInfo->pcmDmaCounter > 1
        ? soundInfo->pcmDmaPeriod - (soundInfo->pcmDmaCounter - 1)
        : 0;
    offset = block * sampleCount;
    if (offset >= PCM_DMA_BUF_SIZE)
        return;
    if (sampleCount > PCM_DMA_BUF_SIZE - offset)
        sampleCount = PCM_DMA_BUF_SIZE - offset;

    right = soundInfo->pcmBuffer + offset;
    left = soundInfo->pcmBuffer + PCM_DMA_BUF_SIZE + offset;
    for (u32 i = 0; i < CGB_CHANNEL_COUNT; i++)
    {
        struct HostCgbOscillator *oscillator = &sOscillators[i];
        struct CgbChannel *channel = &soundInfo->cgbChans[i];

        if (!(channel->statusFlags & SOUND_CHANNEL_SF_ON))
        {
            oscillator->active = FALSE;
            continue;
        }
        if (restart[i] || !oscillator->active)
        {
            oscillator->phase = 0;
            oscillator->noisePhase = 0;
            oscillator->lfsr = 0x7FFF;
            ResetLengthCounter(i, oscillator);
            if (i == 0)
                ResetChannel1Sweep(oscillator);
        }
        oscillator->active = TRUE;
        if (REG_NR52 & SOUND_MASTER_ENABLE)
            MixChannel(i, oscillator, right, left, sampleCount, (u32)soundInfo->pcmFreq);
    }
}

#endif // PORTABLE
