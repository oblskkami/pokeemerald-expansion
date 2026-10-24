#ifdef PORTABLE

#include <string.h>

#include "global.h"
#include "gba/m4a_internal.h"

extern const u8 gClockTable[];

#ifdef _WIN32
extern const char _image_base__[];
extern const char _bss_start__[];
#define M4A_HOST_IMAGE_START _image_base__
#define M4A_HOST_IMAGE_END _bss_start__
#else
extern const char __executable_start[];
extern const char __bss_start[];
#define M4A_HOST_IMAGE_START __executable_start
#define M4A_HOST_IMAGE_END __bss_start
#endif

static bool32 IsValidTrackData(const u8 *ptr, size_t size)
{
    uintptr_t start = (uintptr_t)M4A_HOST_IMAGE_START;
    uintptr_t end = (uintptr_t)M4A_HOST_IMAGE_END;
    uintptr_t address = (uintptr_t)ptr;

    return ptr != NULL && end > start && address >= start
        && address <= end && size <= end - address;
}

static bool32 IsValidTrackTargetRange(const struct MusicPlayerTrack *track,
                                      const u8 *ptr, size_t size)
{
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t trackAddress = (uintptr_t)track;
    uintptr_t localStart;
    uintptr_t localEnd;

    if (IsValidTrackData(ptr, size))
        return TRUE;
    // Focused host tests and callers may provide a short command stream on
    // the stack/heap. Permit a bounded local region for those streams while
    // rejecting null/low addresses and arbitrary tagged words.
    if (ptr == NULL || trackAddress < 0x100000)
        return FALSE;
    localStart = trackAddress - 0x100000;
    localEnd = trackAddress + 0x100000;
    return address >= localStart && address <= localEnd
        && size <= localEnd - address;
}

static bool32 IsValidTrackTarget(const struct MusicPlayerTrack *track, const u8 *ptr)
{
    return IsValidTrackTargetRange(track, ptr, 1);
}

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * multiplicand) >> 32);
}

static u8 ReadTrackByte(struct MusicPlayerTrack *track)
{
    if (track == NULL || !IsValidTrackTarget(track, track->cmdPtr))
        return 0;
    u8 value = *track->cmdPtr;
    track->cmdPtr++;
    return value;
}

static u32 ReadTrackWord(struct MusicPlayerTrack *track)
{
    if (track == NULL || !IsValidTrackTargetRange(track, track->cmdPtr, 4))
        return 0;
    const u8 *ptr = track->cmdPtr;
    u32 value = (u32)ptr[0]
              | ((u32)ptr[1] << 8)
              | ((u32)ptr[2] << 16)
              | ((u32)ptr[3] << 24);
    track->cmdPtr += 4;
    return value;
}

static void SetChannelVolumes(struct SoundChannel *chan, const struct MusicPlayerTrack *track)
{
    u32 right = (u8)(chan->rhythmPan + 128) * chan->velocity * track->volMR >> 14;
    u32 left = (u8)(127 - chan->rhythmPan) * chan->velocity * track->volML >> 14;

    chan->rightVolume = min(right, UINT8_MAX);
    chan->leftVolume = min(left, UINT8_MAX);
}

static const struct ToneData *ResolveTrackTone(const struct MusicPlayerTrack *track, s8 *forcedPan, u8 *key)
{
    const struct ToneData *tone = &track->tone;
    u8 type = tone->type;

    *forcedPan = 0;
    *key = track->key;
    if (type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
    {
        const struct ToneData *group = (const struct ToneData *)tone->wav;
        u8 toneIndex = track->key;

        if (type & TONEDATA_TYPE_SPL)
        {
            const u8 *keySplitTable;
            memcpy(&keySplitTable, &tone->attack, sizeof(keySplitTable));
            if (keySplitTable == NULL)
                return NULL;
            toneIndex = keySplitTable[track->key];
        }
        if (group == NULL)
            return NULL;
        tone = &group[toneIndex];
        if (tone->type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
            return NULL;
        if (type & TONEDATA_TYPE_RHY)
        {
            if (tone->pan_sweep & 0x80)
                *forcedPan = (s8)(((tone->pan_sweep & 0x7F) - C_V) * 2);
            *key = tone->key;
        }
    }
    return tone;
}

static struct SoundChannel *FindDirectSoundChannel(struct SoundInfo *soundInfo, struct MusicPlayerTrack *track, u8 priority)
{
    struct SoundChannel *selected = NULL;
    struct MusicPlayerTrack *selectedTrack = track;
    u8 selectedPriority = priority;
    bool32 foundStopping = FALSE;

    for (u32 i = 0; i < soundInfo->maxChans; i++)
    {
        struct SoundChannel *chan = &soundInfo->chans[i];
        bool32 stopping = (chan->statusFlags & SOUND_CHANNEL_SF_STOP) != 0;

        if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
            return chan;
        if (stopping && !foundStopping)
        {
            foundStopping = TRUE;
            selectedPriority = chan->priority;
            selectedTrack = chan->track;
            selected = chan;
        }
        else if (stopping == foundStopping)
        {
            if (chan->priority < selectedPriority
             || (chan->priority == selectedPriority
              && (uintptr_t)chan->track >= (uintptr_t)selectedTrack))
            {
                selectedPriority = chan->priority;
                selectedTrack = chan->track;
                selected = chan;
            }
        }
    }
    return selected;
}

static s8 CalculateLfoModulation(u8 phase, u8 depth)
{
    s32 wave;
    s32 product;

    if (phase < 0x40)
        wave = phase;
    else if (phase < 0xC0)
        wave = 0x80 - phase;
    else
        wave = phase - 0x100;

    product = depth * wave;
    if (product < 0)
        product = -((-product + 63) >> 6);
    else
        product >>= 6;
    return product;
}

static void UpdateTrackChannels(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo,
                                struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan = track->chan;

    TrkVolPitSet(mplayInfo, track);
    while (chan != NULL)
    {
        struct SoundChannel *next = chan->nextChannelPointer;
        u8 cgbType = chan->type & TONEDATA_TYPE_CGB;

        if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
        {
            RealClearChain(chan);
            chan = next;
            continue;
        }
        if (track->flags & MPT_FLG_VOLCHG)
        {
            SetChannelVolumes(chan, track);
            if (cgbType != 0)
                ((struct CgbChannel *)chan)->modify |= CGB_CHANNEL_MO_VOL;
        }
        if (track->flags & MPT_FLG_PITCHG)
        {
            s32 key = chan->key + (s8)track->keyM;
            if (key < 0)
                key = 0;
            if (cgbType != 0)
            {
                struct CgbChannel *cgbChan = (struct CgbChannel *)chan;
                if (soundInfo->MidiKeyToCgbFreq != NULL)
                    cgbChan->frequency = soundInfo->MidiKeyToCgbFreq(cgbType, key, track->pitM);
                cgbChan->modify |= CGB_CHANNEL_MO_PIT;
            }
            else if (chan->wav != NULL)
            {
                chan->frequency = MidiKeyToFreq(chan->wav, key, track->pitM);
            }
        }
        chan = next;
    }
    track->flags &= 0xF0;
}

void SoundMainBTM(void *addr)
{
    if (addr != NULL)
        memset(addr, 0, 64);
}

void RealClearChain(void *x)
{
    struct SoundChannel *chan = x;
    struct MusicPlayerTrack *track;
    struct SoundChannel *next;
    struct SoundChannel *prev;

    if (chan == NULL || (track = chan->track) == NULL)
        return;

    next = chan->nextChannelPointer;
    prev = chan->prevChannelPointer;
    if (prev != NULL)
        prev->nextChannelPointer = next;
    else
        track->chan = next;
    if (next != NULL)
        next->prevChannelPointer = prev;
    chan->track = NULL;
}

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;
    (void)mplayInfo;

    if (track == NULL)
        return;
    chan = track->chan;
    while (chan != NULL)
    {
        struct SoundChannel *next = chan->nextChannelPointer;
        if (chan->statusFlags & SOUND_CHANNEL_SF_ON)
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
        RealClearChain(chan);
        chan = next;
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        u8 *target = (u8 *)(uintptr_t)ReadTrackWord(track);
        if (IsValidTrackTarget(track, target))
            track->cmdPtr = target;
        else
            ply_fine(mplayInfo, track);
    }
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track == NULL)
        return;
    if (track->patternLevel >= ARRAY_COUNT(track->patternStack))
    {
        ply_fine(mplayInfo, track);
        return;
    }
    track->patternStack[track->patternLevel++] = track->cmdPtr + 4;
    ply_goto(mplayInfo, track);
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL && track->patternLevel != 0)
        track->cmdPtr = track->patternStack[--track->patternLevel];
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 count;
    (void)mplayInfo;
    if (track == NULL)
        return;

    count = *track->cmdPtr;
    if (count == 0)
    {
        track->cmdPtr++;
        ply_goto(mplayInfo, track);
        return;
    }

    track->repN++;
    if (track->repN < count)
    {
        track->cmdPtr++;
        ply_goto(mplayInfo, track);
    }
    else
    {
        track->repN = 0;
        track->cmdPtr += 5;
    }
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
        track->priority = ReadTrackByte(track);
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u16 tempo;
    if (mplayInfo == NULL || track == NULL)
        return;
    tempo = (u16)ReadTrackByte(track) << 1;
    mplayInfo->tempoD = tempo;
    mplayInfo->tempoI = (u16)(((u32)tempo * mplayInfo->tempoU) >> 8);
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->keyShift = ReadTrackByte(track);
        track->flags |= MPT_FLG_PITCHG;
    }
}

void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 voice;
    if (mplayInfo == NULL || track == NULL)
        return;
    voice = ReadTrackByte(track);
    if (mplayInfo->tone != NULL)
        memcpy(&track->tone, &mplayInfo->tone[voice], sizeof(track->tone));
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->vol = ReadTrackByte(track);
        track->flags |= MPT_FLG_VOLCHG;
    }
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->pan = (s8)(ReadTrackByte(track) - C_V);
        track->flags |= MPT_FLG_VOLCHG;
    }
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->bend = (s8)(ReadTrackByte(track) - C_V);
        track->flags |= MPT_FLG_PITCHG;
    }
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->bendRange = ReadTrackByte(track);
        track->flags |= MPT_FLG_PITCHG;
    }
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->lfoSpeed = ReadTrackByte(track);
        if (track->lfoSpeed == 0)
            ClearModM(track);
    }
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
        track->lfoDelay = ReadTrackByte(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->mod = ReadTrackByte(track);
        if (track->mod == 0)
            ClearModM(track);
    }
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 modT;
    (void)mplayInfo;
    if (track == NULL)
        return;
    modT = ReadTrackByte(track);
    if (track->modT != modT)
    {
        track->modT = modT;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    if (track != NULL)
    {
        track->tune = (s8)(ReadTrackByte(track) - C_V);
        track->flags |= MPT_FLG_PITCHG;
    }
}

void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 offset;
    u8 value;

    (void)mplayInfo;
    if (track == NULL)
        return;

    offset = ReadTrackByte(track);
    value = ReadTrackByte(track);
    // The original player addresses byte registers relative to NR10. Keep
    // that command semantics for the process-backed GBA register image while
    // rejecting offsets that would leave the sound-register block.
    if (offset <= REG_OFFSET_FIFO_B - REG_OFFSET_SOUND1CNT_L)
        *(vu8 *)(REG_ADDR_SOUND1CNT_L + offset) = value;
}

void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;
    u8 key;
    (void)mplayInfo;
    if (track == NULL)
        return;
    key = *track->cmdPtr;
    if (key < 0x80)
    {
        track->key = key;
        track->cmdPtr++;
    }
    else
        key = track->key;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if ((chan->statusFlags & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
         && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP)
         && chan->midiKey == key)
        {
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
            break;
        }
    }
}

void ply_note(u32 noteCmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct SoundChannel *chan;
    const struct ToneData *tone;
    s8 forcedPan;
    u8 key;
    u8 cgbType;
    u16 priority;
    s32 transposedKey;

    if (soundInfo == NULL || mplayInfo == NULL || track == NULL)
        return;

    track->gateTime = gClockTable[noteCmd];
    if (*track->cmdPtr < 0x80)
    {
        track->key = ReadTrackByte(track);
        if (*track->cmdPtr < 0x80)
        {
            track->velocity = ReadTrackByte(track);
            if (*track->cmdPtr < 0x80)
                track->gateTime += ReadTrackByte(track);
        }
    }

    tone = ResolveTrackTone(track, &forcedPan, &key);
    if (tone == NULL)
        return;

    priority = mplayInfo->priority + track->priority;
    if (priority > UINT8_MAX)
        priority = UINT8_MAX;
    cgbType = tone->type & TONEDATA_TYPE_CGB;
    if (cgbType != 0)
    {
        if (soundInfo->cgbChans == NULL)
            return;
        chan = (struct SoundChannel *)&soundInfo->cgbChans[cgbType - 1];
        if ((chan->statusFlags & SOUND_CHANNEL_SF_ON)
         && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP)
         && (chan->priority > priority
          || (chan->priority == priority
           && (uintptr_t)chan->track < (uintptr_t)track)))
            return;
    }
    else
    {
        chan = FindDirectSoundChannel(soundInfo, track, priority);
        if (chan == NULL)
            return;
    }

    RealClearChain(chan);
    chan->prevChannelPointer = NULL;
    chan->nextChannelPointer = track->chan;
    if (track->chan != NULL)
        track->chan->prevChannelPointer = chan;
    track->chan = chan;
    chan->track = track;

    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        ClearModM(track);
    TrkVolPitSet(mplayInfo, track);

    chan->gateTime = track->gateTime;
    chan->midiKey = track->key;
    chan->velocity = track->velocity;
    chan->priority = priority;
    chan->key = key;
    chan->rhythmPan = forcedPan;
    chan->type = tone->type;
    chan->wav = tone->wav;
    chan->attack = tone->attack;
    chan->decay = tone->decay;
    chan->sustain = tone->sustain;
    chan->release = tone->release;
    chan->pseudoEchoVolume = track->pseudoEchoVolume;
    chan->pseudoEchoLength = track->pseudoEchoLength;
    SetChannelVolumes(chan, track);

    transposedKey = chan->key + (s8)track->keyM;
    if (transposedKey < 0)
        transposedKey = 0;
    if (cgbType != 0)
    {
        struct CgbChannel *cgbChan = (struct CgbChannel *)chan;
        cgbChan->length = tone->length;
        cgbChan->sweep = (tone->pan_sweep & 0x80) || !(tone->pan_sweep & 0x70)
            ? 8
            : tone->pan_sweep;
        if (soundInfo->MidiKeyToCgbFreq != NULL)
            cgbChan->frequency = soundInfo->MidiKeyToCgbFreq(cgbType, transposedKey, track->pitM);
    }
    else
    {
        chan->count = track->unk_3C;
        if (chan->wav != NULL)
            chan->frequency = MidiKeyToFreq(chan->wav, transposedKey, track->pitM);
    }

    chan->statusFlags = SOUND_CHANNEL_SF_START;
    track->flags &= 0xF0;
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;

    if (mplayInfo == NULL || soundInfo == NULL || mplayInfo->ident != ID_NUMBER)
        return;
    mplayInfo->ident = ID_NUMBER + 1;

    if (mplayInfo->MPlayMainNext != NULL)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);
    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;

    FadeOutBody(mplayInfo);
    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;

    mplayInfo->tempoC += mplayInfo->tempoI;
    while (mplayInfo->tempoC >= 150)
    {
        u16 trackBits = 0;

        for (u32 i = 0; i < mplayInfo->trackCount; i++)
        {
            struct MusicPlayerTrack *track = &mplayInfo->tracks[i];
            struct SoundChannel *chan;

            if (!(track->flags & MPT_FLG_EXIST))
                continue;
            trackBits |= 1u << i;

            chan = track->chan;
            while (chan != NULL)
            {
                struct SoundChannel *next = chan->nextChannelPointer;
                if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                    RealClearChain(chan);
                else if (chan->gateTime != 0 && --chan->gateTime == 0)
                    chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
                chan = next;
            }

            if (track->flags & MPT_FLG_START)
            {
                SoundMainBTM(track);
                track->flags = MPT_FLG_EXIST;
                track->bendRange = 2;
                track->volX = 64;
                track->lfoSpeed = 22;
                track->tone.type = 1;
            }

            // A malformed command stream can otherwise execute forever in a
            // single VBlank (for example, a self-referential goto).  Valid
            // songs stay well below this bound; bad streams are stopped.
            u32 commandBudget = 4096;
            while (track->wait == 0)
            {
                if (commandBudget-- == 0
                 || !IsValidTrackTarget(track, track->cmdPtr))
                {
                    ply_fine(mplayInfo, track);
                    goto nextTrack;
                }
                u8 event = *track->cmdPtr;
                if (event < 0x80)
                    event = track->runningStatus;
                else
                {
                    track->cmdPtr++;
                    if (event >= 0xBD)
                        track->runningStatus = event;
                }

                if (event >= 0xCF)
                {
                    if (soundInfo->plynote != NULL)
                        soundInfo->plynote(event - 0xCF, mplayInfo, track);
                }
                else if (event >= 0xB1)
                {
                    MPlayFunc eventFunc;
                    mplayInfo->cmd = event - 0xB1;
                    eventFunc = soundInfo->MPlayJumpTable[mplayInfo->cmd];
                    if (eventFunc != NULL)
                        eventFunc(mplayInfo, track);
                    if (track->flags == 0)
                        goto nextTrack;
                }
                else
                {
                    track->wait = gClockTable[event - 0x80];
                }
            }

            track->wait--;
            if (track->lfoSpeed != 0 && track->mod != 0)
            {
                s8 modulation;
                if (track->lfoDelayC != 0)
                {
                    track->lfoDelayC--;
                    goto nextTrack;
                }
                track->lfoSpeedC += track->lfoSpeed;
                modulation = CalculateLfoModulation(track->lfoSpeedC, track->mod);
                if (modulation != track->modM)
                {
                    track->modM = modulation;
                    track->flags |= track->modT == 0 ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG;
                }
            }

nextTrack:
            continue;
        }

        mplayInfo->clock++;
        if (trackBits == 0)
        {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mplayInfo->status = trackBits;
        mplayInfo->tempoC -= 150;
    }

    for (u32 i = 0; i < mplayInfo->trackCount; i++)
    {
        struct MusicPlayerTrack *track = &mplayInfo->tracks[i];
        if ((track->flags & MPT_FLG_EXIST) && (track->flags & 0x0F))
            UpdateTrackChannels(soundInfo, mplayInfo, track);
    }

done:
    mplayInfo->ident = ID_NUMBER;
}

void m4aSoundVSync(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    if (soundInfo == NULL || soundInfo->ident < ID_NUMBER || soundInfo->ident > ID_NUMBER + 1)
        return;
    if (soundInfo->pcmDmaCounter != 0)
        soundInfo->pcmDmaCounter--;
    if (soundInfo->pcmDmaCounter == 0)
        soundInfo->pcmDmaCounter = soundInfo->pcmDmaPeriod;
}

static s8 ClampPcmSample(s32 sample)
{
    if (sample < -128)
        return -128;
    if (sample > 127)
        return 127;
    return sample;
}

static u32 DirectSoundStepQ16(const struct SoundInfo *soundInfo,
                              const struct SoundChannel *chan)
{
    u32 frequency = chan->frequency;
    u64 step;

    // TONEDATA_TYPE_FIX is the driver's no-resample DirectSound voice type.
    if ((chan->type & TONEDATA_TYPE_FIX) != 0
     || frequency == 0 || soundInfo->divFreq == 0)
        return 1 << 16;

    // The target mixer advances a 9.23 cursor by frequency * divFreq. Convert
    // that exact cursor to Q16 for the host sidecar without floating point.
    step = ((u64)frequency * soundInfo->divFreq) >> 7;
    if (step == 0)
        step = 1;
    if (step > UINT32_MAX)
        step = UINT32_MAX;
    return (u32)step;
}

static bool32 DirectSoundLoopEnabled(const struct WaveData *wav)
{
    return (wav->status & WAVE_DATA_FLAG_LOOP) != 0
        && wav->loopStart < wav->size;
}

static bool32 TickDirectSoundEnvelope(const struct SoundInfo *soundInfo,
                                      struct SoundChannel *chan)
{
    const struct WaveData *wav = chan->wav;
    u8 status = chan->statusFlags;
    u32 envelope;
    u32 masterEnvelope;

    if (!(status & SOUND_CHANNEL_SF_ON))
        return FALSE;

    if (status & SOUND_CHANNEL_SF_START)
    {
        if ((status & SOUND_CHANNEL_SF_STOP) || wav == NULL
         || wav->size == 0 || chan->count > wav->size)
            goto stop;

        status = SOUND_CHANNEL_SF_ENV_ATTACK;
        chan->currentPointer = (s8 *)wav->data + chan->count;
        chan->count = wav->size - chan->count;
        chan->fw = 0;
        envelope = 0;
        if (DirectSoundLoopEnabled(wav))
            status |= SOUND_CHANNEL_SF_LOOP;
        goto attack;
    }

    envelope = chan->envelopeVolume;
    if (status & SOUND_CHANNEL_SF_IEC)
    {
        u8 echoLength = chan->pseudoEchoLength;

        chan->pseudoEchoLength = echoLength - 1;
        if (echoLength <= 1)
            goto stop;
        goto updateGains;
    }

    if (status & SOUND_CHANNEL_SF_STOP)
    {
        envelope = (envelope * chan->release) >> 8;
        if (envelope > chan->pseudoEchoVolume)
            goto updateGains;
        goto startEcho;
    }

    switch (status & SOUND_CHANNEL_SF_ENV)
    {
    case SOUND_CHANNEL_SF_ENV_DECAY:
        envelope = (envelope * chan->decay) >> 8;
        if (envelope > chan->sustain)
            break;
        envelope = chan->sustain;
        if (envelope == 0)
            goto startEcho;
        status--;
        break;
    case SOUND_CHANNEL_SF_ENV_ATTACK:
attack:
        envelope += chan->attack;
        if (envelope >= UINT8_MAX)
        {
            envelope = UINT8_MAX;
            status--;
        }
        break;
    default:
        break;
    }
    goto updateGains;

startEcho:
    envelope = chan->pseudoEchoVolume;
    if (envelope == 0)
        goto stop;
    status |= SOUND_CHANNEL_SF_IEC;

updateGains:
    chan->statusFlags = status;
    chan->envelopeVolume = envelope;
    masterEnvelope = (envelope * (soundInfo->masterVolume + 1)) >> 4;
    chan->envelopeVolumeRight = (chan->rightVolume * masterEnvelope) >> 8;
    chan->envelopeVolumeLeft = (chan->leftVolume * masterEnvelope) >> 8;
    return TRUE;

stop:
    chan->statusFlags = 0;
    return FALSE;
}

static void MixDirectSoundChannel(const struct SoundInfo *soundInfo,
                                   struct SoundChannel *chan,
                                   s8 *right, s8 *left, u32 sampleCount)
{
    const struct WaveData *wav = chan->wav;
    u32 position;
    u32 remaining = chan->count;
    u32 fraction = chan->fw & 0xFFFF;
    u32 step = DirectSoundStepQ16(soundInfo, chan);
    u32 gainRight = chan->envelopeVolumeRight;
    u32 gainLeft = chan->envelopeVolumeLeft;
    uintptr_t dataStart;
    uintptr_t current;

    if (wav == NULL || wav->size == 0 || remaining == 0
     || remaining > wav->size)
    {
        chan->statusFlags = 0;
        chan->count = 0;
        chan->fw = 0;
        return;
    }

    dataStart = (uintptr_t)wav->data;
    current = (uintptr_t)chan->currentPointer;
    if (current < dataStart || current >= dataStart + wav->size)
    {
        chan->statusFlags = 0;
        chan->count = 0;
        chan->fw = 0;
        return;
    }
    position = (u32)(current - dataStart);

    for (u32 i = 0; i < sampleCount; i++)
    {
        s32 sample = wav->data[position];
        u64 nextFraction = (u64)fraction + step;
        u32 advance = (u32)(nextFraction >> 16);
        s32 mixed;

        // SoundMainRAM interpolates with the current 9.23 phase, then advances
        // the cursor. Use the equivalent Q16 phase here.
        if (fraction != 0)
        {
            s32 nextSample;

            if (position + 1 < wav->size)
                nextSample = wav->data[position + 1];
            else if (DirectSoundLoopEnabled(wav))
                nextSample = wav->data[wav->loopStart];
            else
                nextSample = 0;
            sample += (nextSample - sample) * (s32)fraction >> 16;
        }

        mixed = (sample * (s32)gainRight) >> 8;
        right[i] = ClampPcmSample((s32)right[i] + mixed);
        mixed = (sample * (s32)gainLeft) >> 8;
        left[i] = ClampPcmSample((s32)left[i] + mixed);

        fraction = (u32)nextFraction & 0xFFFF;
        if (advance == 0)
            continue;
        if (advance < remaining)
        {
            remaining -= advance;
            position += advance;
            continue;
        }

        if (DirectSoundLoopEnabled(wav))
        {
            u32 loopLength = wav->size - wav->loopStart;
            u32 overshoot = (advance - remaining) % loopLength;

            position = wav->loopStart + overshoot;
            remaining = loopLength - overshoot;
            continue;
        }

        chan->statusFlags = 0;
        chan->currentPointer = (s8 *)wav->data + wav->size;
        chan->count = 0;
        chan->fw = 0;
        return;
    }

    chan->currentPointer = (s8 *)wav->data + position;
    chan->count = remaining;
    chan->fw = fraction;
}

static s8 DecodeDpcmSample(const struct WaveData *wav, u32 position)
{
    static const s8 sDpcmDeltas[16] =
    {
          0,   1,   4,   9,  16,  25,  36,  49,
        -64, -49, -36, -25, -16,  -9,  -4,  -1,
    };
    const u8 *block = (const u8 *)wav->data + (position >> 6) * 33;
    u32 inBlock = position & 63;
    u8 sample = block[0];

    for (u32 i = 1; i <= inBlock; i++)
    {
        u8 packed;
        u8 delta;

        if (i == 1)
        {
            packed = block[1];
            delta = packed & 0xF;
        }
        else
        {
            u32 tailSamples = wav->size & 63;

            // wav2agb --no-pad does not emit the final high nibble of an odd
            // partial block. The ARM data stream reads zero from its .align
            // padding; model that delta directly instead of reading past the
            // binary payload on the host.
            if (i == inBlock && position == wav->size - 1
             && tailSamples > 1 && (tailSamples & 1) != 0)
            {
                delta = 0;
            }
            else
            {
                packed = block[2 + (i - 2) / 2];
                delta = (i & 1) != 0 ? packed & 0xF : packed >> 4;
            }
        }
        sample += sDpcmDeltas[delta];
    }
    return (s8)sample;
}

static s8 ReadSpecialDirectSoundSample(const struct WaveData *wav, u32 position)
{
    if (wav->type != 0)
        return DecodeDpcmSample(wav, position);
    return wav->data[position];
}

static void StoreSpecialDirectSoundCursor(struct SoundChannel *chan,
                                          const struct WaveData *wav,
                                          u32 position, bool32 reverse)
{
    u32 cursor = position + (reverse ? 1 : 0);

    if (wav->type != 0)
        chan->currentPointer = (s8 *)(uintptr_t)cursor;
    else
        chan->currentPointer = (s8 *)wav->data + cursor;
}

static void StopSpecialDirectSoundChannel(struct SoundChannel *chan,
                                          const struct WaveData *wav,
                                          bool32 reverse)
{
    chan->statusFlags = 0;
    chan->count = 0;
    chan->fw = 0;
    if (wav->type != 0)
        chan->currentPointer = (s8 *)(uintptr_t)(reverse ? 0 : wav->size);
    else
        chan->currentPointer = (s8 *)wav->data + (reverse ? 0 : wav->size);
}

static void MixSpecialDirectSoundChannel(const struct SoundInfo *soundInfo,
                                          struct SoundChannel *chan,
                                          s8 *right, s8 *left, u32 sampleCount)
{
    const struct WaveData *wav = chan->wav;
    bool32 reverse = (chan->type & TONEDATA_TYPE_REV) != 0;
    bool32 firstBlock = (chan->statusFlags & SOUND_CHANNEL_SF_SPECIAL) == 0;
    u32 position;
    u32 remaining = chan->count;
    u32 fraction = chan->fw & 0xFFFF;
    u32 step = DirectSoundStepQ16(soundInfo, chan);
    u32 gainRight = chan->envelopeVolumeRight;
    u32 gainLeft = chan->envelopeVolumeLeft;

    chan->statusFlags |= SOUND_CHANNEL_SF_SPECIAL;

    // A compression flag with an uncompressed, non-reversed wave is not a
    // valid M4A voice. The target marks it special but emits no samples.
    if (wav->type == 0 && !reverse)
        return;

    if (remaining == 0)
    {
        StopSpecialDirectSoundChannel(chan, wav, reverse);
        return;
    }

    if (firstBlock)
    {
        position = reverse ? remaining - 1 : wav->size - remaining;
    }
    else if (wav->type != 0)
    {
        uintptr_t cursor = (uintptr_t)chan->currentPointer;
        if (reverse && cursor == 0)
        {
            StopSpecialDirectSoundChannel(chan, wav, reverse);
            return;
        }
        position = (u32)cursor - (reverse ? 1 : 0);
    }
    else
    {
        uintptr_t dataStart = (uintptr_t)wav->data;
        uintptr_t cursor = (uintptr_t)chan->currentPointer;
        u32 adjustment = reverse ? 1 : 0;

        if (cursor < dataStart + adjustment
         || cursor > dataStart + wav->size)
        {
            StopSpecialDirectSoundChannel(chan, wav, reverse);
            return;
        }
        position = (u32)(cursor - dataStart) - adjustment;
    }

    if (position >= wav->size)
    {
        StopSpecialDirectSoundChannel(chan, wav, reverse);
        return;
    }

    for (u32 i = 0; i < sampleCount; i++)
    {
        s32 sample = ReadSpecialDirectSoundSample(wav, position);
        u32 nextPosition = position;
        u64 nextFraction = (u64)fraction + step;
        u32 advance = (u32)(nextFraction >> 16);
        s32 mixed;

        if (fraction != 0)
        {
            bool32 haveNext = FALSE;

            if (reverse)
            {
                if (position > 0)
                {
                    nextPosition--;
                    haveNext = TRUE;
                }
            }
            else if (position + 1 < wav->size)
            {
                nextPosition++;
                haveNext = TRUE;
            }
            else if (DirectSoundLoopEnabled(wav))
            {
                nextPosition = wav->loopStart;
                haveNext = TRUE;
            }

            if (haveNext)
            {
                s32 nextSample = ReadSpecialDirectSoundSample(wav, nextPosition);
                sample += (nextSample - sample) * (s32)fraction >> 16;
            }
        }

        mixed = (sample * (s32)gainRight) >> 8;
        right[i] = ClampPcmSample((s32)right[i] + mixed);
        mixed = (sample * (s32)gainLeft) >> 8;
        left[i] = ClampPcmSample((s32)left[i] + mixed);

        fraction = (u32)nextFraction & 0xFFFF;
        if (advance == 0)
            continue;

        if (advance < remaining)
        {
            remaining -= advance;
            position = reverse ? position - advance : position + advance;
            continue;
        }

        if (!reverse && DirectSoundLoopEnabled(wav))
        {
            u32 loopLength = wav->size - wav->loopStart;
            u32 overshoot = (advance - remaining) % loopLength;

            position = wav->loopStart + overshoot;
            remaining = loopLength - overshoot;
            continue;
        }

        StopSpecialDirectSoundChannel(chan, wav, reverse);
        return;
    }

    chan->count = remaining;
    chan->fw = fraction;
    StoreSpecialDirectSoundCursor(chan, wav, position, reverse);
}

static void PrepareDirectSoundBuffer(const struct SoundInfo *soundInfo,
                                     s8 *right, s8 *left, u32 sampleCount,
                                     u32 offset)
{
    u32 nextOffset;

    if (soundInfo->reverb == 0)
    {
        memset(right, 0, sampleCount);
        memset(left, 0, sampleCount);
        return;
    }

    // SoundMainRAM feeds back the current DMA block and the following block;
    // counter 2 is the ring-buffer wrap from the final block to block zero.
    nextOffset = soundInfo->pcmDmaCounter == 2 ? 0 : offset + sampleCount;
    if (nextOffset > PCM_DMA_BUF_SIZE - sampleCount)
        nextOffset = 0;

    for (u32 i = 0; i < sampleCount; i++)
    {
        s32 sample = right[i] + left[i]
                   + soundInfo->pcmBuffer[nextOffset + i]
                   + soundInfo->pcmBuffer[PCM_DMA_BUF_SIZE + nextOffset + i];

        sample = sample * soundInfo->reverb >> 9;
        if (sample & 0x80)
            sample++;
        right[i] = sample;
        left[i] = sample;
    }
}

void HostMixDirectSound(struct SoundInfo *soundInfo)
{
    u32 block;
    u32 offset;
    u32 sampleCount;
    s8 *right;
    s8 *left;

    if (soundInfo == NULL
     || (soundInfo->ident != ID_NUMBER && soundInfo->ident != ID_NUMBER + 1)
     || soundInfo->pcmSamplesPerVBlank <= 0
     || soundInfo->pcmSamplesPerVBlank > PCM_DMA_BUF_SIZE
     || soundInfo->pcmDmaPeriod == 0)
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
    PrepareDirectSoundBuffer(soundInfo, right, left, sampleCount, offset);

    for (u32 i = 0; i < soundInfo->maxChans && i < MAX_DIRECTSOUND_CHANNELS; i++)
    {
        struct SoundChannel *chan = &soundInfo->chans[i];
        if (!TickDirectSoundEnvelope(soundInfo, chan)
         || (chan->type & TONEDATA_TYPE_CGB) != 0)
            continue;
        if ((chan->type & (TONEDATA_TYPE_REV | TONEDATA_TYPE_CMP)) != 0)
            MixSpecialDirectSoundChannel(soundInfo, chan, right, left, sampleCount);
        else
            MixDirectSoundChannel(soundInfo, chan, right, left, sampleCount);
    }
}

void HostApplyDirectSoundRouting(struct SoundInfo *soundInfo)
{
    u32 block;
    u32 offset;
    u32 sampleCount;
    u16 control;
    u32 gainA;
    u32 gainB;
    s8 *fifoA;
    s8 *fifoB;

    if (soundInfo == NULL
     || (soundInfo->ident != ID_NUMBER && soundInfo->ident != ID_NUMBER + 1)
     || soundInfo->pcmSamplesPerVBlank <= 0
     || soundInfo->pcmSamplesPerVBlank > PCM_DMA_BUF_SIZE
     || soundInfo->pcmDmaPeriod == 0)
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

    control = REG_SOUNDCNT_H;
    gainA = (control & SOUND_A_MIX_FULL) != 0 ? 256 : 128;
    gainB = (control & SOUND_B_MIX_FULL) != 0 ? 256 : 128;
    fifoA = soundInfo->pcmBuffer + offset;
    fifoB = soundInfo->pcmBuffer + PCM_DMA_BUF_SIZE + offset;

    // Keep the FIFO halves as the intermediate representation until after
    // DirectSound has rendered. CGB audio then adds its already-routed output.
    for (u32 i = 0; i < sampleCount; i++)
    {
        s32 a = fifoA[i];
        s32 b = fifoB[i];
        s32 right = 0;
        s32 left = 0;

        if (control & SOUND_A_RIGHT_OUTPUT)
            right += (a * (s32)gainA) >> 8;
        if (control & SOUND_B_RIGHT_OUTPUT)
            right += (b * (s32)gainB) >> 8;
        if (control & SOUND_A_LEFT_OUTPUT)
            left += (a * (s32)gainA) >> 8;
        if (control & SOUND_B_LEFT_OUTPUT)
            left += (b * (s32)gainB) >> 8;

        fifoA[i] = ClampPcmSample(right);
        fifoB[i] = ClampPcmSample(left);
    }
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct SoundChannel *chan;
    (void)mplayInfo;
    if (track == NULL || !(track->flags & MPT_FLG_EXIST))
        return;

    chan = track->chan;
    while (chan != NULL)
    {
        struct SoundChannel *next = chan->nextChannelPointer;
        u8 cgbType = chan->type & TONEDATA_TYPE_CGB;
        if (chan->statusFlags != 0 && cgbType != 0
         && soundInfo != NULL && soundInfo->CgbOscOff != NULL)
            soundInfo->CgbOscOff(cgbType);
        chan->statusFlags = 0;
        chan->track = NULL;
        chan = next;
    }
    track->chan = NULL;
}

#endif // PORTABLE
