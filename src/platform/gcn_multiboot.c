#ifdef PORTABLE

#include <string.h>

#include "gba/types.h"
#include "libgcnmultiboot.h"

void GameCubeMultiBoot_Init(struct GcmbStruct *state)
{
    if (state != NULL)
        memset(state, 0, sizeof(*state));
}

void GameCubeMultiBoot_Main(struct GcmbStruct *state)
{
    if (state != NULL)
        state->gcmb_field_2 = 0;
}

void GameCubeMultiBoot_ExecuteProgram(struct GcmbStruct *state)
{
    (void)state;
}

void GameCubeMultiBoot_HandleSerialInterrupt(struct GcmbStruct *state)
{
    if (state != NULL)
        state->gcmb_field_2 = 0;
}

void GameCubeMultiBoot_Quit(void)
{}

#endif // PORTABLE
