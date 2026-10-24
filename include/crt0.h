#ifndef GUARD_CRT0_H
#define GUARD_CRT0_H

#ifdef PORTABLE
extern void IntrMain(void);
#else
extern u32 IntrMain[];
#endif

extern void ReInitializeEWRAM(void);

#endif //GUARD_CRT0_H
