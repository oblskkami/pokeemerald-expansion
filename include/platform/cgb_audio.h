#ifndef GUARD_PLATFORM_CGB_AUDIO_H
#define GUARD_PLATFORM_CGB_AUDIO_H

struct SoundInfo;

// Advances the target M4A CGB channels and mixes their four hardware-style
// oscillators into the current target-layout PCM block. This is an M4A channel
// backend, not a standalone emulator for arbitrary sound-register writes.
void HostMixCgbAudio(struct SoundInfo *soundInfo);

#endif // GUARD_PLATFORM_CGB_AUDIO_H
