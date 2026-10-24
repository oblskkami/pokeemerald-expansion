#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "field_name_box.h"
#include "main.h"
#include "script.h"
#include "constants/script_commands.h"
#include "constants/speaker_names.h"

typedef u16 (*SpecialFunc)(void);

#ifdef _WIN32
extern const char _bss_start__[];
#define HOST_IMAGE_END _bss_start__
#else
extern const char __bss_start[];
#define HOST_IMAGE_END __bss_start
#endif
extern ScrCmdFunc gScriptCmdTable[];
extern ScrCmdFunc gScriptCmdTableEnd[];
extern const SpecialFunc gSpecials[];
extern bool8 ScrCmd_nop(struct ScriptContext *ctx);
extern const u8 SPECIAL_GetPlayerFacingDirection;
extern enum Direction GetPlayerFacingDirection(void);

static int sFailures;
static bool8 sCallNativeReached;
static bool8 sGotoNativeReached;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "script pointer smoke failed: %s:%d: %s\n",     \
                    __FILE__, __LINE__, #expression);                        \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

static void WriteTaggedPointer(u8 *dest, const void *func)
{
    uintptr_t tagged = Script_EncodeEffectAddress(func);

    memcpy(dest, &tagged, sizeof(tagged));
}

static void TestCallNative(struct ScriptContext *ctx)
{
    (void)ctx;
    sCallNativeReached = TRUE;
}

static bool8 TestGotoNative(void)
{
    sGotoNativeReached = TRUE;
    return TRUE;
}

static void TestTablePointers(void)
{
    uintptr_t commandAddress = (uintptr_t)gScriptCmdTable[SCR_OP_NOP];
    uintptr_t specialId = (uintptr_t)&SPECIAL_GetPlayerFacingDirection;
    SpecialFunc taggedSpecial;

    CHECK(gScriptCmdTableEnd - gScriptCmdTable > SCR_OP_WAITSTATE);
    CHECK(Script_IsEffectInstrumentedAddress(commandAddress));
    CHECK(Script_DecodeEffectAddress(commandAddress) == (uintptr_t)ScrCmd_nop);

    taggedSpecial = gSpecials[specialId];
    CHECK(Script_IsEffectInstrumentedAddress((uintptr_t)taggedSpecial));
    CHECK(Script_DecodeEffectAddress((uintptr_t)taggedSpecial)
        == (uintptr_t)GetPlayerFacingDirection);
}

static void TestCommandDispatch(void)
{
    struct ScriptContext context;
    const u8 script[] = {SCR_OP_WAITSTATE};

    InitScriptContext(&context, gScriptCmdTable, gScriptCmdTableEnd);
    SetupBytecodeScript(&context, script);
    CHECK(RunScriptCommand(&context) == TRUE);
}

static void TestCallNativeTag(void)
{
    struct ScriptContext context;
    u8 callScript[] = {SCR_OP_CALLNATIVE, 0, 0, 0, 0, SCR_OP_WAITSTATE};
    u8 gotoScript[] = {SCR_OP_GOTONATIVE, 0, 0, 0, 0};
    u8 specialScript[] = {SCR_OP_SPECIAL, 0, 0, SCR_OP_WAITSTATE};
    uintptr_t specialId = (uintptr_t)&SPECIAL_GetPlayerFacingDirection;

    WriteTaggedPointer(&callScript[1], (const void *)TestCallNative);
    CHECK(Script_MatchesCallNative(callScript, (void *)TestCallNative, TRUE));
    CHECK(!Script_MatchesCallNative(callScript, (void *)TestCallNative, FALSE));

    sCallNativeReached = FALSE;
    InitScriptContext(&context, gScriptCmdTable, gScriptCmdTableEnd);
    SetupBytecodeScript(&context, callScript);
    CHECK(RunScriptCommand(&context) == TRUE);
    CHECK(sCallNativeReached == TRUE);

    WriteTaggedPointer(&gotoScript[1], (const void *)TestGotoNative);
    sGotoNativeReached = FALSE;
    InitScriptContext(&context, gScriptCmdTable, gScriptCmdTableEnd);
    SetupBytecodeScript(&context, gotoScript);
    CHECK(RunScriptCommand(&context) == TRUE);
    CHECK(sGotoNativeReached == FALSE);
    CHECK(RunScriptCommand(&context) == TRUE);
    CHECK(sGotoNativeReached == TRUE);

    specialScript[1] = specialId;
    specialScript[2] = specialId >> 8;
    CHECK(Script_MatchesSpecial(specialScript, (void *)GetPlayerFacingDirection));
    InitScriptContext(&context, gScriptCmdTable, gScriptCmdTableEnd);
    SetupBytecodeScript(&context, specialScript);
    CHECK(RunScriptCommand(&context) == TRUE);
}

static void TestSpeakerPointerBoundary(void)
{
    struct ScriptContext context = {0};
    u8 script[sizeof(u32)];
    u32 address;

    address = (u32)(uintptr_t)gSpeakerNamesTable[SP_NAME_PLAYER];
    memcpy(script, &address, sizeof(address));
    context.scriptPtr = script;
    SetSpeaker(&context);
    CHECK(gSpeakerName == gSpeakerNamesTable[SP_NAME_PLAYER]);

    address = (u32)(uintptr_t)HOST_IMAGE_END;
    memcpy(script, &address, sizeof(address));
    context.scriptPtr = script;
    SetSpeaker(&context);
    CHECK(gSpeakerName == NULL);
}

static void TestTrainerIdTimer(void)
{
    StartTimer1();
    for (volatile u32 i = 0; i < 100000; i++)
        ;
    SeedRngAndSetTrainerId();

    CHECK(REG_TM1CNT_H == 0);
    CHECK(REG_TM2CNT_H == 0);
    CHECK(REG_TM1CNT_L != 0 || REG_TM2CNT_L != 0);
}

static void TestCycleCounter(void)
{
    CycleCountStart();
    for (volatile u32 i = 0; i < 100000; i++)
        ;
    CHECK(CycleCountEnd() != 0);
}

int main(void)
{
    TestTablePointers();
    TestCommandDispatch();
    TestCallNativeTag();
    TestSpeakerPointerBoundary();
    TestTrainerIdTimer();
    TestCycleCounter();

    if (sFailures != 0)
        return 1;
    puts("script pointer smoke checks passed");
    return 0;
}
