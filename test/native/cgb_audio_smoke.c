#include <stdio.h>
#include <string.h>

#include "gba/gba.h"
#include "gba/m4a_internal.h"
#include "platform/cgb_audio.h"

const u8 gCgb3Vol[] =
{
    0x00, 0x00,
    0x60, 0x60, 0x60, 0x60,
    0x40, 0x40, 0x40, 0x40,
    0x80, 0x80, 0x80, 0x80,
    0x20, 0x20,
};

static struct SoundInfo sSoundInfo;

static void ResetSoundInfo(u32 sampleCount, u32 sampleRate)
{
    memset(REG_BASE, 0, 0x400);
    memset(&sSoundInfo, 0, sizeof(sSoundInfo));
    memset(gCgbChans, 0, sizeof(struct CgbChannel) * 4);
    SOUND_INFO_PTR = &sSoundInfo;
    sSoundInfo.ident = ID_NUMBER + 1;
    sSoundInfo.cgbChans = gCgbChans;
    sSoundInfo.CgbSound = CgbSound;
    sSoundInfo.pcmSamplesPerVBlank = sampleCount;
    sSoundInfo.pcmDmaPeriod = PCM_DMA_BUF_SIZE / sampleCount;
    sSoundInfo.pcmFreq = sampleRate;
    REG_SOUNDCNT_X = SOUND_MASTER_ENABLE;
    REG_SOUNDCNT_H = SOUND_CGB_MIX_FULL;
    REG_NR50 = 0x77;
    gCgbChans[0].panMask = 0x11;
    gCgbChans[1].panMask = 0x22;
    gCgbChans[2].panMask = 0x44;
    gCgbChans[3].panMask = 0x88;
}

static void StartChannel(u32 channel, u8 volume)
{
    struct CgbChannel *cgb = &gCgbChans[channel];

    cgb->statusFlags = SOUND_CHANNEL_SF_START;
    cgb->type = channel + 1;
    cgb->rightVolume = volume;
    cgb->leftVolume = volume;
    cgb->attack = 0;
    cgb->decay = 0;
    cgb->sustain = 15;
    cgb->release = 0;
}

static int TestSquareAndPan(void)
{
    ResetSoundInfo(8, 512);
    StartChannel(0, 120);
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    gCgbChans[0].frequency = 0;
    HostMixCgbAudio(&sSoundInfo);

    for (u32 i = 0; i < 8; i++)
    {
        s8 expected = i < 4 ? 24 : -24;
        if (sSoundInfo.pcmBuffer[i] != expected
         || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + i] != expected)
            return 0;
    }
    if (gCgbChans[0].statusFlags != SOUND_CHANNEL_SF_ENV_SUSTAIN
     || gCgbChans[0].envelopeVolume != 15)
        return 0;

    ResetSoundInfo(8, 512);
    StartChannel(0, 240);
    gCgbChans[0].leftVolume = 0;
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    HostMixCgbAudio(&sSoundInfo);
    if (sSoundInfo.pcmBuffer[0] != 24
     || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 0
     || (REG_NR51 & 0x11) != 0x01)
        return 0;
    return 1;
}

static int TestWaveChannel(void)
{
    u32 wave[4] = {0};
    u8 *bytes = (u8 *)wave;

    bytes[0] = 0xF0;
    bytes[1] = 0x81;
    ResetSoundInfo(4, 1024);
    StartChannel(2, 120);
    gCgbChans[2].wavePointer = wave;
    gCgbChans[2].frequency = 0;
    HostMixCgbAudio(&sSoundInfo);

    return sSoundInfo.pcmBuffer[0] == 24
        && sSoundInfo.pcmBuffer[1] == -24
        && sSoundInfo.pcmBuffer[2] == 1
        && sSoundInfo.pcmBuffer[3] == -20
        && memcmp(sSoundInfo.pcmBuffer,
                  sSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE, 4) == 0;
}

static int TestNoiseAndMasterDisable(void)
{
    s8 reference[64];
    int hasPositive = 0;
    int hasNegative = 0;

    ResetSoundInfo(64, 524288);
    StartChannel(3, 120);
    gCgbChans[3].frequency = 0;
    HostMixCgbAudio(&sSoundInfo);
    for (u32 i = 0; i < 64; i++)
    {
        hasPositive |= sSoundInfo.pcmBuffer[i] > 0;
        hasNegative |= sSoundInfo.pcmBuffer[i] < 0;
    }
    if (!hasPositive || !hasNegative)
        return 0;
    memcpy(reference, sSoundInfo.pcmBuffer, sizeof(reference));

    // NR43 divisor code 2 clocks at one quarter of code 0.
    ResetSoundInfo(64, 131072);
    StartChannel(3, 120);
    gCgbChans[3].frequency = 2;
    HostMixCgbAudio(&sSoundInfo);
    if (memcmp(reference, sSoundInfo.pcmBuffer, sizeof(reference)) != 0)
        return 0;

    ResetSoundInfo(4, 512);
    memset(sSoundInfo.pcmBuffer, 7, sizeof(sSoundInfo.pcmBuffer));
    StartChannel(0, 120);
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    REG_SOUNDCNT_X = 0;
    HostMixCgbAudio(&sSoundInfo);
    return sSoundInfo.pcmBuffer[0] == 7
        && sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 7;
}

static int TestSilentEnvelopeAdvancesPhase(void)
{
    ResetSoundInfo(2, 512);
    StartChannel(0, 0);
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    gCgbChans[0].frequency = 0;
    HostMixCgbAudio(&sSoundInfo);

    gCgbChans[0].statusFlags = SOUND_CHANNEL_SF_ENV_SUSTAIN;
    gCgbChans[0].envelopeVolume = 15;
    REG_NR12 = 0xF0;
    REG_NR51 = 0x11;
    HostMixCgbAudio(&sSoundInfo);

    return sSoundInfo.pcmBuffer[0] == 24
        && sSoundInfo.pcmBuffer[1] == 24
        && sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 24;
}

static int TestMixGainAndClipping(void)
{
    ResetSoundInfo(1, 512);
    sSoundInfo.pcmBuffer[0] = 120;
    sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] = -120;
    StartChannel(0, 120);
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    REG_SOUNDCNT_H = SOUND_CGB_MIX_HALF;
    HostMixCgbAudio(&sSoundInfo);
    if (sSoundInfo.pcmBuffer[0] != 127
     || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != -108)
        return 0;

    ResetSoundInfo(1, 512);
    StartChannel(0, 120);
    gCgbChans[0].wavePointer = (u32 *)(uintptr_t)2;
    REG_SOUNDCNT_H = SOUND_CGB_MIX_QUARTER;
    HostMixCgbAudio(&sSoundInfo);
    return sSoundInfo.pcmBuffer[0] == 6
        && sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 6;
}

static void AdvanceCgbSamples(u32 sampleCount)
{
    while (sampleCount-- != 0)
        HostMixCgbAudio(&sSoundInfo);
}

static int TestChannel1Sweep(void)
{
    ResetSoundInfo(1, 1024);
    StartChannel(0, 120);
    gCgbChans[0].sweep = 0x11; // one 128 Hz tick, increase by half
    gCgbChans[0].frequency = 1000;
    AdvanceCgbSamples(8);
    if ((REG_SOUND1CNT_X & 0x7FF) != 1500)
        return 0;

    ResetSoundInfo(1, 1024);
    StartChannel(0, 120);
    gCgbChans[0].sweep = 0x19; // one 128 Hz tick, decrease by half
    gCgbChans[0].frequency = 1000;
    AdvanceCgbSamples(8);
    if ((REG_SOUND1CNT_X & 0x7FF) != 500)
        return 0;

    ResetSoundInfo(1, 1024);
    StartChannel(0, 120);
    gCgbChans[0].sweep = 0x11;
    gCgbChans[0].frequency = 2000;
    AdvanceCgbSamples(8);
    sSoundInfo.pcmBuffer[0] = 17;
    HostMixCgbAudio(&sSoundInfo);
    if (sSoundInfo.pcmBuffer[0] != 17)
        return 0;

    // Overflow remains muted until a new START reloads the shadow frequency.
    gCgbChans[0].statusFlags = SOUND_CHANNEL_SF_START;
    gCgbChans[0].frequency = 1000;
    sSoundInfo.pcmBuffer[0] = 0;
    HostMixCgbAudio(&sSoundInfo);
    if (sSoundInfo.pcmBuffer[0] != 24)
        return 0;

    ResetSoundInfo(1, 1024);
    StartChannel(0, 120);
    gCgbChans[0].sweep = 8; // target's explicit sweep-disabled value
    gCgbChans[0].frequency = 1000;
    AdvanceCgbSamples(8);
    return (REG_SOUND1CNT_X & 0x7FF) == 1000;
}

static void ConfigureLengthChannel(u32 channel, u8 length)
{
    static u32 sWave[4] =
    {
        0xF0F0F0F0,
        0xF0F0F0F0,
        0xF0F0F0F0,
        0xF0F0F0F0,
    };

    StartChannel(channel, 120);
    gCgbChans[channel].length = length;
    gCgbChans[channel].frequency = channel == 3 ? 0xF7 : 0;
    gCgbChans[channel].wavePointer = channel == 2
        ? sWave
        : (u32 *)(uintptr_t)2;
}

static u8 LengthControl(u32 channel)
{
    switch (channel)
    {
    case 0:
        return REG_NR14;
    case 1:
        return REG_NR24;
    case 2:
        return REG_NR34;
    default:
        return REG_NR44;
    }
}

static int TestLengthCounters(void)
{
    static const u8 sOneTickLengths[] = {63, 63, 255, 63};

    for (u32 channel = 0; channel < 4; channel++)
    {
        ResetSoundInfo(1, 256);
        ConfigureLengthChannel(channel, sOneTickLengths[channel]);
        HostMixCgbAudio(&sSoundInfo);
        if (!(LengthControl(channel) & 0x40)
         || sSoundInfo.pcmBuffer[0] == 0
         || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 0)
            return 0;

        memset(sSoundInfo.pcmBuffer, 0, sizeof(sSoundInfo.pcmBuffer));
        HostMixCgbAudio(&sSoundInfo);
        if (sSoundInfo.pcmBuffer[0] != 0
         || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 0)
            return 0;

        // A new trigger reloads the hardware length and makes the channel audible.
        gCgbChans[channel].statusFlags = SOUND_CHANNEL_SF_START;
        HostMixCgbAudio(&sSoundInfo);
        if (sSoundInfo.pcmBuffer[0] == 0
         || sSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] == 0)
            return 0;
    }

    // Length is clocked at 256 Hz, independently of the output sample rate.
    ResetSoundInfo(5, 1024);
    ConfigureLengthChannel(0, 63);
    HostMixCgbAudio(&sSoundInfo);
    for (u32 sample = 0; sample < 4; sample++)
    {
        if (sSoundInfo.pcmBuffer[sample] == 0)
            return 0;
    }
    if (sSoundInfo.pcmBuffer[4] != 0)
        return 0;

    // Square/noise use a 6-bit length. A register value of 1 lasts 63 ticks.
    ResetSoundInfo(1, 256);
    ConfigureLengthChannel(1, 1);
    for (u32 tick = 0; tick < 63; tick++)
    {
        memset(sSoundInfo.pcmBuffer, 0, sizeof(sSoundInfo.pcmBuffer));
        HostMixCgbAudio(&sSoundInfo);
        if (sSoundInfo.pcmBuffer[0] == 0)
            return 0;
    }
    memset(sSoundInfo.pcmBuffer, 0, sizeof(sSoundInfo.pcmBuffer));
    HostMixCgbAudio(&sSoundInfo);
    if (sSoundInfo.pcmBuffer[0] != 0)
        return 0;

    // The target leaves bit 6 clear when the tone length is zero.
    ResetSoundInfo(1, 256);
    ConfigureLengthChannel(0, 0);
    for (u32 tick = 0; tick < 65; tick++)
    {
        memset(sSoundInfo.pcmBuffer, 0, sizeof(sSoundInfo.pcmBuffer));
        HostMixCgbAudio(&sSoundInfo);
        if ((LengthControl(0) & 0x40) || sSoundInfo.pcmBuffer[0] == 0)
            return 0;
    }
    return 1;
}

int main(void)
{
    if (!TestSquareAndPan())
    {
        fprintf(stderr, "native CGB square/pan checks failed\n");
        return 1;
    }
    if (!TestWaveChannel())
    {
        fprintf(stderr, "native CGB wave checks failed\n");
        return 1;
    }
    if (!TestNoiseAndMasterDisable())
    {
        fprintf(stderr, "native CGB noise/master checks failed\n");
        return 1;
    }
    if (!TestSilentEnvelopeAdvancesPhase())
    {
        fprintf(stderr, "native CGB silent-envelope phase checks failed\n");
        return 1;
    }
    if (!TestMixGainAndClipping())
    {
        fprintf(stderr, "native CGB mix-gain checks failed\n");
        return 1;
    }
    if (!TestChannel1Sweep())
    {
        fprintf(stderr, "native CGB channel-1 sweep checks failed\n");
        return 1;
    }
    if (!TestLengthCounters())
    {
        fprintf(stderr, "native CGB length-counter checks failed\n");
        return 1;
    }
    puts("native CGB audio smoke checks passed");
    return 0;
}
