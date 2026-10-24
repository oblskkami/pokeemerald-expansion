#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "gba/m4a_internal.h"
#include "gba/gba.h"
#include "m4a.h"
#include "platform.h"

_Static_assert(sizeof(struct SoundChannel) == 64, "SoundChannel must retain the target layout");
_Static_assert(offsetof(struct SoundInfo, chans) == 80, "SoundInfo channel offset must match GBA");
_Static_assert(offsetof(struct SoundInfo, pcmBuffer) == 848, "SoundInfo PCM offset must match GBA");

const u8 gScaleTable[180] = {0};
const u32 gFreqTable[12] = {2147483648u};
const u8 gClockTable[49] = {[1] = 1, [4] = 4};
const u16 gPcmSamplesPerVBlankTable[] =
{
    96, 132, 176, 224, 264, 304,
    352, 448, 528, 608, 672, 704,
};

// This focused mixer test does not link the SDL platform object.  Keep the
// queue boundary explicit while leaving device I/O to the SDL executable.
void Platform_QueueAudio(const s8 *right, const s8 *left, u32 sampleCount,
                         u32 sampleRate)
{
    (void)right;
    (void)left;
    (void)sampleCount;
    (void)sampleRate;
}

static int IsZeroed(const void *data, size_t size)
{
    const u8 *bytes = data;
    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

static int TestHostCommandHandlers(void)
{
    struct MusicPlayerInfo info = {0};
    struct MusicPlayerTrack track = {0};
    struct SoundChannel first = {0};
    struct SoundChannel second = {0};
    struct ToneData tones[2] = {0};
    u8 commands[] = {
        0x23, 0x45, 0x67, 0x02, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x99,
        REG_OFFSET_NR51 - REG_OFFSET_SOUND1CNT_L, 0xBB,
    };
    u8 clear[64];

    memset(clear, 0xA5, sizeof(clear));
    SoundMainBTM(clear);
    if (!IsZeroed(clear, sizeof(clear)))
        return 0;

    first.track = &track;
    first.nextChannelPointer = &second;
    second.track = &track;
    second.prevChannelPointer = &first;
    track.chan = &first;
    RealClearChain(&first);
    if (track.chan != &second || second.prevChannelPointer != NULL || first.track != NULL)
        return 0;
    RealClearChain(&second);
    if (track.chan != NULL || second.track != NULL)
        return 0;

    track.flags = MPT_FLG_EXIST;
    track.chan = &first;
    first.track = &track;
    first.statusFlags = SOUND_CHANNEL_SF_ON;
    first.nextChannelPointer = &second;
    second.track = &track;
    second.statusFlags = SOUND_CHANNEL_SF_ON;
    TrackStop(&info, &track);
    if (track.chan != NULL || first.statusFlags != 0 || second.statusFlags != 0
     || first.track != NULL || second.track != NULL)
        return 0;

    info.tempoU = 0x100;
    track.cmdPtr = commands;
    ply_prio(&info, &track);
    ply_tempo(&info, &track);
    ply_keysh(&info, &track);
    ply_voice(&info, &track);
    ply_vol(&info, &track);
    ply_pan(&info, &track);
    ply_bend(&info, &track);
    ply_bendr(&info, &track);
    ply_lfos(&info, &track);
    ply_lfodl(&info, &track);
    ply_mod(&info, &track);
    ply_modt(&info, &track);
    ply_tune(&info, &track);
    ply_port(&info, &track);
    if (track.priority != 0x23 || info.tempoD != 0x8A || info.tempoI != 0x8A
     || track.keyShift != 0x67 || track.vol != 0x11 || track.pan != (s8)(0x22 - C_V)
     || track.bend != (s8)(0x33 - C_V) || track.bendRange != 0x44
     || track.lfoSpeed != 0x55 || track.lfoDelay != 0x66 || track.mod != 0x77
     || track.modT != 0x88 || track.tune != (s8)(0x99 - C_V)
     || REG_NR51 != 0xBB || track.cmdPtr != commands + sizeof(commands))
        return 0;

    commands[0] = REG_OFFSET_FIFO_B - REG_OFFSET_SOUND1CNT_L + 1;
    commands[1] = 0x5A;
    track.cmdPtr = commands;
    REG_BASE[REG_OFFSET_FIFO_B + 1] = 0xA5;
    ply_port(&info, &track);
    if (REG_BASE[REG_OFFSET_FIFO_B + 1] != 0xA5 || track.cmdPtr != commands + 2)
        return 0;

    memset(&track, 0, sizeof(track));
    memset(&info, 0, sizeof(info));
    tones[1].type = TONEDATA_TYPE_FIX;
    tones[1].attack = 0xAB;
    info.tone = tones;
    commands[0] = 1;
    track.cmdPtr = commands;
    ply_voice(&info, &track);
    if (track.tone.type != TONEDATA_TYPE_FIX || track.tone.attack != 0xAB)
        return 0;

    return 1;
}

static int TestHostNote(void)
{
    struct MusicPlayerInfo info = {0};
    struct MusicPlayerTrack track = {0};
    struct WaveData wave = {.freq = 1024};
    u8 note[] = {60, 100, 2, 0x80};
    struct SoundChannel *chan;

    memset(gSoundInfo.chans, 0, sizeof(gSoundInfo.chans));
    gSoundInfo.maxChans = 2;
    info.priority = 250;
    track.priority = 20;
    track.flags = MPT_FLG_EXIST | MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    track.vol = 127;
    track.volX = 64;
    track.tone.type = 0;
    track.tone.wav = &wave;
    track.tone.attack = 3;
    track.tone.decay = 4;
    track.tone.sustain = 5;
    track.tone.release = 6;
    track.cmdPtr = note;

    ply_note(4, &info, &track);
    chan = track.chan;
    if (chan != &gSoundInfo.chans[0]
     || chan->statusFlags != SOUND_CHANNEL_SF_START
     || chan->track != &track
     || chan->gateTime != gClockTable[4] + 2
     || chan->midiKey != 60 || chan->key != 60 || chan->velocity != 100
     || chan->priority != UINT8_MAX || chan->wav != &wave
     || chan->attack != 3 || chan->decay != 4 || chan->sustain != 5 || chan->release != 6
     || track.cmdPtr != &note[3] || (track.flags & 0x0F) != 0)
        return 0;
    return 1;
}

static int TestHostSequencer(void)
{
    struct MusicPlayerInfo info = {0};
    struct MusicPlayerTrack track = {0};
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[1];
    } wave = {0, WAVE_DATA_FLAG_LOOP, 1024, 0, 1, {0}};
    MPlayFunc jumpTable[36] = {0};
    u8 sequence[] = {0xD0, 60, 100, 0x81, 0xB1};

    memset(gSoundInfo.chans, 0, sizeof(gSoundInfo.chans));
    gSoundInfo.maxChans = 2;
    gSoundInfo.MPlayJumpTable = jumpTable;
    gSoundInfo.plynote = ply_note;
    gSoundInfo.MPlayMainHead = MPlayMain;
    gSoundInfo.musicPlayerHead = &info;
    gSoundInfo.ident = ID_NUMBER;
    jumpTable[0] = (MPlayFunc)ply_fine;

    info.status = 1;
    info.trackCount = 1;
    info.tempoI = 150;
    info.tracks = &track;
    info.ident = ID_NUMBER;
    track.flags = MPT_FLG_EXIST | MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    track.vol = 127;
    track.volX = 64;
    track.tone.wav = (struct WaveData *)&wave;
    track.tone.attack = UINT8_MAX;
    track.cmdPtr = sequence;

    m4aSoundMain();
    if (info.ident != ID_NUMBER || info.clock != 1 || info.status != 1
     || track.chan != &gSoundInfo.chans[0]
     || track.chan->statusFlags != (SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY)
     || track.chan->gateTime != 1 || track.cmdPtr != &sequence[4])
        return 0;

    m4aSoundMain();
    if (info.ident != ID_NUMBER || info.clock != 2 || info.status != 1
     || track.flags != 0 || track.chan != NULL
     || gSoundInfo.chans[0].track != NULL)
        return 0;

    m4aSoundMain();
    if (info.ident != ID_NUMBER || info.clock != 3
     || info.status != MUSICPLAYER_STATUS_PAUSE)
        return 0;
    return 1;
}

static int TestHostDirectSound(void)
{
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[4];
    } sample = {0, 0, 1024 * 13379, 0, 4, {10, -20, 30, -40}};
    struct SoundChannel *chan = &gSoundInfo.chans[0];

    memset(gSoundInfo.pcmBuffer, 0x5A, sizeof(gSoundInfo.pcmBuffer));
    memset(chan, 0, sizeof(*chan));
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = TONEDATA_TYPE_FIX;
    chan->wav = (struct WaveData *)&sample;
    chan->frequency = sample.freq;
    chan->rightVolume = 128;
    chan->leftVolume = 255;
    chan->attack = UINT8_MAX;
    chan->decay = UINT8_MAX;
    chan->sustain = UINT8_MAX;
    chan->release = UINT8_MAX;
    gSoundInfo.ident = ID_NUMBER + 1;
    gSoundInfo.maxChans = 1;
    gSoundInfo.masterVolume = 15;
    gSoundInfo.pcmSamplesPerVBlank = 4;
    gSoundInfo.pcmDmaPeriod = 4;
    gSoundInfo.pcmDmaCounter = 0;

    HostMixDirectSound(&gSoundInfo);
    if (gSoundInfo.pcmBuffer[0] != 4
     || gSoundInfo.pcmBuffer[1] != -10
     || gSoundInfo.pcmBuffer[2] != 14
     || gSoundInfo.pcmBuffer[3] != -20
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 9
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 1] != -20
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 2] != 29
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 3] != -40
     || gSoundInfo.pcmBuffer[4] != 0x5A
     || chan->statusFlags != 0
     || chan->envelopeVolume != UINT8_MAX
     || chan->envelopeVolumeRight != 127
     || chan->envelopeVolumeLeft != 254
     || chan->currentPointer != sample.data + 4)
        return 0;

    sample.status = 0;
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = 0;
    chan->frequency = 6690;
    chan->count = 0;
    chan->currentPointer = NULL;
    chan->fw = 0;
    HostMixDirectSound(&gSoundInfo);
    if (gSoundInfo.pcmBuffer[0] != 4
     || gSoundInfo.pcmBuffer[1] != -3
     || gSoundInfo.pcmBuffer[2] != -10
     || gSoundInfo.pcmBuffer[3] != 2
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 9
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 1] != -6
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 2] != -20
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE + 3] != 4
     || chan->currentPointer != sample.data + 2)
        return 0;

    sample.status = WAVE_DATA_FLAG_LOOP;
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = TONEDATA_TYPE_FIX;
    chan->frequency = sample.freq;
    chan->count = 0;
    chan->currentPointer = NULL;
    chan->fw = 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->currentPointer != sample.data
     || chan->statusFlags != (SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY)
     || gSoundInfo.pcmBuffer[0] != 4
     || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 9)
        return 0;

    chan->release = 0;
    chan->pseudoEchoVolume = 0;
    chan->statusFlags = SOUND_CHANNEL_SF_STOP;
    memset(gSoundInfo.pcmBuffer, 0x7B, sizeof(gSoundInfo.pcmBuffer));
    HostMixDirectSound(&gSoundInfo);
    if (gSoundInfo.pcmBuffer[0] != 0 || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 0)
        return 0;

    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = TONEDATA_TYPE_CMP;
    chan->count = 0;
    sample.status = 0;
    HostMixDirectSound(&gSoundInfo);
    if (gSoundInfo.pcmBuffer[0] != 0
     || chan->statusFlags != (SOUND_CHANNEL_SF_SPECIAL | SOUND_CHANNEL_SF_ENV_DECAY)
     || chan->envelopeVolume != UINT8_MAX)
        return 0;
    return 1;
}

static int TestHostDirectSoundRouting(void)
{
    s8 *fifoA = gSoundInfo.pcmBuffer;
    s8 *fifoB = gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE;

    memset(gSoundInfo.pcmBuffer, 0, sizeof(gSoundInfo.pcmBuffer));
    gSoundInfo.ident = ID_NUMBER + 1;
    gSoundInfo.pcmSamplesPerVBlank = 2;
    gSoundInfo.pcmDmaPeriod = 1;
    gSoundInfo.pcmDmaCounter = 0;
    fifoA[0] = 100;
    fifoA[1] = -100;
    fifoB[0] = 60;
    fifoB[1] = -60;
    REG_SOUNDCNT_H = SOUND_A_RIGHT_OUTPUT | SOUND_B_LEFT_OUTPUT
                   | SOUND_A_MIX_FULL | SOUND_B_MIX_FULL;
    HostApplyDirectSoundRouting(&gSoundInfo);
    if (fifoA[0] != 100 || fifoA[1] != -100
     || fifoB[0] != 60 || fifoB[1] != -60)
        return 0;

    fifoA[0] = 100;
    fifoA[1] = -100;
    fifoB[0] = 60;
    fifoB[1] = -60;
    REG_SOUNDCNT_H = SOUND_A_RIGHT_OUTPUT | SOUND_A_LEFT_OUTPUT
                   | SOUND_B_RIGHT_OUTPUT | SOUND_B_LEFT_OUTPUT;
    HostApplyDirectSoundRouting(&gSoundInfo);
    if (fifoA[0] != 80 || fifoA[1] != -80
     || fifoB[0] != 80 || fifoB[1] != -80)
        return 0;

    fifoA[0] = 127;
    fifoB[0] = 127;
    REG_SOUNDCNT_H = SOUND_A_RIGHT_OUTPUT | SOUND_B_RIGHT_OUTPUT
                   | SOUND_A_MIX_FULL | SOUND_B_MIX_FULL;
    HostApplyDirectSoundRouting(&gSoundInfo);
    return fifoA[0] == 127 && fifoB[0] == 0;
}

static void InitSpecialDirectSoundChannel(struct SoundChannel *chan,
                                          struct WaveData *wav, u8 type)
{
    memset(chan, 0, sizeof(*chan));
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = type;
    chan->wav = wav;
    chan->frequency = wav->freq;
    chan->rightVolume = UINT8_MAX;
    chan->leftVolume = UINT8_MAX;
    chan->attack = UINT8_MAX;
    chan->decay = UINT8_MAX;
    chan->sustain = UINT8_MAX;
    chan->release = UINT8_MAX;
}

static int TestHostSpecialDirectSound(void)
{
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[4];
    } compressed = {1, 0, 1024 * 13379, 0, 6, {10, 0x02, 0x19, 0x70}};
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[4];
    } uncompressed = {0, WAVE_DATA_FLAG_LOOP, 1024 * 13379, 1, 4,
                      {10, -20, 30, -40}};
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[35];
    } twoBlocks = {1, 0, 1024 * 13379, 0, 66,
                   {[33] = 100, [34] = 1}};
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[3];
        s8 payloadCanary;
    } oddTail = {1, 0, 1024 * 13379, 0, 5, {10, 0x02, 0x19}, 0x70};
    static const s8 sCompressedForward[] = {9, 13, 14, -34, 14, 14};
    static const s8 sCompressedReverse[] = {14, 14, -34, 14, 13, 9};
    static const s8 sUncompressedReverse[] = {29, -20, 9};
    static const s8 sCompressedBlockBoundary[] = {0, 0, 99, 100};
    static const s8 sCompressedOddTail[] = {9, 13, 14, -34, -34};
    static const s8 sCompressedLoop[] = {9, 13, 14, -34, 14, 14, 14, -34};
    static const s8 sCompressedHalfRate[] = {9, 11, 13, 13};
    struct SoundChannel *chan = &gSoundInfo.chans[0];

    gSoundInfo.ident = ID_NUMBER + 1;
    gSoundInfo.maxChans = 1;
    gSoundInfo.masterVolume = 15;
    gSoundInfo.pcmDmaPeriod = 1;
    gSoundInfo.pcmDmaCounter = 0;

    gSoundInfo.pcmSamplesPerVBlank = sizeof(sCompressedForward);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&compressed,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_CMP);
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedForward,
               sizeof(sCompressedForward)) != 0
     || memcmp(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE, sCompressedForward,
               sizeof(sCompressedForward)) != 0
     || chan->statusFlags != 0 || chan->count != 0
     || (uintptr_t)chan->currentPointer != compressed.size)
        return 0;

    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&compressed,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV);
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedReverse,
               sizeof(sCompressedReverse)) != 0
     || chan->statusFlags != 0 || chan->count != 0
     || (uintptr_t)chan->currentPointer != 0)
        return 0;

    gSoundInfo.pcmSamplesPerVBlank = sizeof(sUncompressedReverse);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&uncompressed,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_REV);
    chan->count = 1;
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sUncompressedReverse,
               sizeof(sUncompressedReverse)) != 0
     || chan->statusFlags != 0 || chan->count != 0
     || chan->currentPointer != uncompressed.data)
        return 0;

    gSoundInfo.pcmSamplesPerVBlank = sizeof(sCompressedBlockBoundary);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&twoBlocks,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_CMP);
    chan->count = 62;
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedBlockBoundary,
               sizeof(sCompressedBlockBoundary)) != 0
     || chan->statusFlags != 0 || chan->count != 0
     || (uintptr_t)chan->currentPointer != twoBlocks.size)
        return 0;

    gSoundInfo.pcmSamplesPerVBlank = sizeof(sCompressedOddTail);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&oddTail,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_CMP);
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedOddTail,
               sizeof(sCompressedOddTail)) != 0
     || oddTail.payloadCanary != 0x70 || chan->statusFlags != 0
     || (uintptr_t)chan->currentPointer != oddTail.size)
        return 0;

    compressed.status = WAVE_DATA_FLAG_LOOP;
    compressed.loopStart = 2;
    gSoundInfo.pcmSamplesPerVBlank = sizeof(sCompressedLoop);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&compressed,
                                  TONEDATA_TYPE_FIX | TONEDATA_TYPE_CMP);
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedLoop,
               sizeof(sCompressedLoop)) != 0
     || chan->statusFlags != (SOUND_CHANNEL_SF_SPECIAL | SOUND_CHANNEL_SF_LOOP
                            | SOUND_CHANNEL_SF_ENV_DECAY)
     || chan->count != 2 || (uintptr_t)chan->currentPointer != 4)
        return 0;

    compressed.status = 0;
    compressed.loopStart = 0;
    gSoundInfo.divFreq = 128;
    gSoundInfo.pcmSamplesPerVBlank = sizeof(sCompressedHalfRate);
    InitSpecialDirectSoundChannel(chan, (struct WaveData *)&compressed,
                                  TONEDATA_TYPE_CMP);
    chan->frequency = 32768;
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer, sCompressedHalfRate,
               sizeof(sCompressedHalfRate)) != 0
     || chan->statusFlags != (SOUND_CHANNEL_SF_SPECIAL | SOUND_CHANNEL_SF_ENV_DECAY)
     || chan->count != 4 || (uintptr_t)chan->currentPointer != 2
     || chan->fw != 0)
        return 0;

    return 1;
}

static int TestHostReverb(void)
{
    static const s8 sExpected[] = {12, -12, 50, -49};
    static const s8 sCurrent[] = {10, -10, 100, -100};
    static const s8 sCurrentLeft[] = {20, -20, 100, -100};
    static const s8 sNext[] = {30, -30, 100, -100};
    static const s8 sNextLeft[] = {40, -40, 100, -100};
    u32 offset = 8;
    u32 nextOffset = 12;

    memset(gSoundInfo.pcmBuffer, 0x5A, sizeof(gSoundInfo.pcmBuffer));
    memcpy(gSoundInfo.pcmBuffer + offset, sCurrent, sizeof(sCurrent));
    memcpy(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE + offset,
           sCurrentLeft, sizeof(sCurrentLeft));
    memcpy(gSoundInfo.pcmBuffer + nextOffset, sNext, sizeof(sNext));
    memcpy(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE + nextOffset,
           sNextLeft, sizeof(sNextLeft));
    gSoundInfo.ident = ID_NUMBER + 1;
    gSoundInfo.maxChans = 0;
    gSoundInfo.reverb = 64;
    gSoundInfo.pcmSamplesPerVBlank = 4;
    gSoundInfo.pcmDmaPeriod = 4;
    gSoundInfo.pcmDmaCounter = 3;

    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer + offset, sExpected, sizeof(sExpected)) != 0
     || memcmp(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE + offset,
               sExpected, sizeof(sExpected)) != 0
     || gSoundInfo.pcmBuffer[offset - 1] != 0x5A)
        return 0;

    // Counter 2 mixes the final DMA block with block zero.
    memset(gSoundInfo.pcmBuffer, 0, sizeof(gSoundInfo.pcmBuffer));
    offset = 12;
    memcpy(gSoundInfo.pcmBuffer + offset, sCurrent, sizeof(sCurrent));
    memcpy(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE + offset,
           sCurrentLeft, sizeof(sCurrentLeft));
    memcpy(gSoundInfo.pcmBuffer, sNext, sizeof(sNext));
    memcpy(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE, sNextLeft, sizeof(sNextLeft));
    gSoundInfo.pcmDmaCounter = 2;
    HostMixDirectSound(&gSoundInfo);
    if (memcmp(gSoundInfo.pcmBuffer + offset, sExpected, sizeof(sExpected)) != 0
     || memcmp(gSoundInfo.pcmBuffer + PCM_DMA_BUF_SIZE + offset,
               sExpected, sizeof(sExpected)) != 0)
        return 0;

    gSoundInfo.reverb = 0;
    return 1;
}

static int TestHostDirectSoundEnvelope(void)
{
    struct
    {
        u16 type;
        u16 status;
        u32 freq;
        u32 loopStart;
        u32 size;
        s8 data[1];
    } sample = {0, WAVE_DATA_FLAG_LOOP, 1024 * 13379, 0, 1, {64}};
    struct SoundChannel *chan = &gSoundInfo.chans[0];

    memset(chan, 0, sizeof(*chan));
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = TONEDATA_TYPE_FIX;
    chan->wav = (struct WaveData *)&sample;
    chan->frequency = sample.freq;
    chan->rightVolume = 200;
    chan->leftVolume = 100;
    chan->attack = 100;
    chan->decay = 128;
    chan->sustain = 40;
    chan->release = 128;
    chan->pseudoEchoVolume = 16;
    chan->pseudoEchoLength = 2;
    gSoundInfo.ident = ID_NUMBER + 1;
    gSoundInfo.maxChans = 1;
    gSoundInfo.masterVolume = 7;
    gSoundInfo.pcmSamplesPerVBlank = 1;
    gSoundInfo.pcmDmaPeriod = 1;
    gSoundInfo.pcmDmaCounter = 0;

    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != (SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_ATTACK)
     || chan->envelopeVolume != 100
     || chan->envelopeVolumeRight != 39 || chan->envelopeVolumeLeft != 19
     || gSoundInfo.pcmBuffer[0] != 9 || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 4)
        return 0;

    HostMixDirectSound(&gSoundInfo);
    if (chan->envelopeVolume != 200
     || chan->envelopeVolumeRight != 78 || chan->envelopeVolumeLeft != 39)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != (SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY)
     || chan->envelopeVolume != UINT8_MAX
     || chan->envelopeVolumeRight != 99 || chan->envelopeVolumeLeft != 49)
        return 0;

    HostMixDirectSound(&gSoundInfo);
    if (chan->envelopeVolume != 127
     || chan->envelopeVolumeRight != 49 || chan->envelopeVolumeLeft != 24)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->envelopeVolume != 63
     || chan->envelopeVolumeRight != 24 || chan->envelopeVolumeLeft != 12)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != (SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_SUSTAIN)
     || chan->envelopeVolume != 40
     || chan->envelopeVolumeRight != 15 || chan->envelopeVolumeLeft != 7)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->envelopeVolume != 40)
        return 0;

    chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != (SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_SUSTAIN)
     || chan->envelopeVolume != 20
     || chan->envelopeVolumeRight != 7 || chan->envelopeVolumeLeft != 3)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != (SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_LOOP
                            | SOUND_CHANNEL_SF_IEC | SOUND_CHANNEL_SF_ENV_SUSTAIN)
     || chan->envelopeVolume != 16 || chan->pseudoEchoLength != 2
     || chan->envelopeVolumeRight != 6 || chan->envelopeVolumeLeft != 3)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->envelopeVolume != 16 || chan->pseudoEchoLength != 1)
        return 0;
    HostMixDirectSound(&gSoundInfo);
    if (chan->statusFlags != 0 || chan->pseudoEchoLength != 0
     || gSoundInfo.pcmBuffer[0] != 0 || gSoundInfo.pcmBuffer[PCM_DMA_BUF_SIZE] != 0)
        return 0;
    return 1;
}

static int TestUninitializedPokemonCry(void)
{
    // The opening sequence can query cry state before any cry player exists.
    // A missing player is a quiet, valid "not playing" state.
    struct MusicPlayerInfo info = {0};

    return IsPokemonCryPlaying(NULL) == FALSE
        && IsPokemonCryPlaying(&info) == FALSE;
}

int main(void)
{
    memset(REG_BASE, 0, 0x400);
    memset(&gSoundInfo, 0, sizeof(gSoundInfo));
    SOUND_INFO_PTR = &gSoundInfo;
    gSoundInfo.ident = ID_NUMBER;

    // SampleFreqSet used to wait forever for a real GBA scanline under PORTABLE.
    SampleFreqSet(SOUND_MODE_FREQ_13379);

    if (gSoundInfo.freq != 4
     || gSoundInfo.pcmSamplesPerVBlank == 0
     || gSoundInfo.pcmDmaPeriod == 0)
    {
        fprintf(stderr, "native M4A timing initialization failed\n");
        return 1;
    }

    if (!TestUninitializedPokemonCry())
    {
        fprintf(stderr, "native uninitialized Pokemon cry check failed\n");
        return 1;
    }

    if (!TestHostCommandHandlers())
    {
        fprintf(stderr, "native M4A host command checks failed\n");
        return 1;
    }

    if (!TestHostNote())
    {
        fprintf(stderr, "native M4A host note checks failed\n");
        return 1;
    }

    if (!TestHostSequencer())
    {
        fprintf(stderr, "native M4A host sequencer checks failed\n");
        return 1;
    }

    if (!TestHostDirectSound())
    {
        fprintf(stderr, "native DirectSound host mixer checks failed\n");
        return 1;
    }

    if (!TestHostDirectSoundRouting())
    {
        fprintf(stderr, "native DirectSound routing checks failed\n");
        return 1;
    }

    if (!TestHostSpecialDirectSound())
    {
        fprintf(stderr, "native compressed/reverse DirectSound checks failed\n");
        return 1;
    }

    if (!TestHostReverb())
    {
        fprintf(stderr, "native DirectSound reverb checks failed\n");
        return 1;
    }

    if (!TestHostDirectSoundEnvelope())
    {
        fprintf(stderr, "native DirectSound envelope checks failed\n");
        return 1;
    }

    puts("native M4A timing smoke checks passed");
    return 0;
}
