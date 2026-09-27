#include "global.h"
#include "event_data.h"
#include "field_move.h"
#include "item.h"
#include "party_menu.h"
#include "pokemon.h"
#include "test/overworld_script.h"
#include "test/test.h"
#include "config/overworld.h"
#include "constants/field_move.h"
#include "constants/items.h"
#include "constants/moves.h"

ASSUMPTIONS
{
    ASSUME(CanLearnTeachableMove(SPECIES_ZIGZAGOON, MOVE_CUT));
    ASSUME(CanLearnTeachableMove(SPECIES_ZIGZAGOON, MOVE_DIG));
    ASSUME(!CanLearnTeachableMove(SPECIES_ZIGZAGOON, MOVE_FLY));
    ASSUME(CanLearnTeachableMove(SPECIES_TAILLOW, MOVE_FLY));
    ASSUME(CanLearnTeachableMove(SPECIES_ABRA, MOVE_FLASH));
    ASSUME(!CanLearnTeachableMove(SPECIES_WOBBUFFET, MOVE_CUT));
    ASSUME(!CanLearnTeachableMove(SPECIES_WOBBUFFET, MOVE_DIG));
    ASSUME(!CanLearnTeachableMove(SPECIES_WOBBUFFET, MOVE_FLY));
    ASSUME(!CanLearnTeachableMove(SPECIES_WOBBUFFET, MOVE_FLASH));
}

// Field moves unlocked by a badge store the badge as an offset from
// FLAG_BADGE01_GET, which differs between the Emerald and FRLG builds.
static void UnlockFieldMove(enum FieldMove fieldMove)
{
    FlagSet(gFieldMoveInfo[fieldMove].arg + FLAG_BADGE01_GET);
}

static void MakeSlotAnEgg(u32 slotId)
{
    bool32 isEgg = TRUE;
    SetMonData(&gParties[B_TRAINER_PLAYER][slotId], MON_DATA_IS_EGG, &isEgg);
}

static void TeachSlotMove(u32 slotId, enum Move move)
{
    u16 moveId = move;
    SetMonData(&gParties[B_TRAINER_PLAYER][slotId], MON_DATA_MOVE1, &moveId);
}

TEST("checkfieldmove returns the slot of a party mon that can learn the move")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_CUT);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
        givemon SPECIES_ZIGZAGOON, 5;
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, 1);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_POKEMON);
}

TEST("checkfieldmove fails without the badge even when a party mon can learn the move")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE);
}

TEST("checkfieldmove falls back to the key item when no party mon can learn the move")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_CUT);
    AddBagItem(ITEM_CUT_TOOL, 1);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE + 1);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_ITEM);
}

TEST("checkfieldmove fails when the key item is held but the badge is missing")
{
    ZeroPlayerPartyMons();
    AddBagItem(ITEM_CUT_TOOL, 1);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE);
}

TEST("checkfieldmove skips eggs that could otherwise learn the move")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_CUT);
    AddBagItem(ITEM_CUT_TOOL, 1);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
    );
    MakeSlotAnEgg(0);
    RUN_OVERWORLD_SCRIPT(
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE + 1);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_ITEM);
}

TEST("checkfieldmove succeeds for an always-unlocked field move without a badge")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
    );
    TeachSlotMove(0, MOVE_DIG);
    RUN_OVERWORLD_SCRIPT(
        checkfieldmove FIELD_MOVE_DIG, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, 0);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_POKEMON);
}

// Only field moves with a key item alternative are relaxed to "can learn"; the
// rest keep the vanilla requirement so that moves like Secret Power, which have
// no tool, are not usable by every mon that could be taught them.
TEST("checkfieldmove requires a known move when the field move has no key item")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
        checkfieldmove FIELD_MOVE_DIG, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE);
}

TEST("checkfieldmove fails for a field move with no key item and no capable mon")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
        checkfieldmove FIELD_MOVE_DIG, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE);
}

TEST("checkfieldmove fails with an empty party and an empty bag")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_CUT);
    RUN_OVERWORLD_SCRIPT(
        checkfieldmove FIELD_MOVE_CUT, TRUE;
    );
    EXPECT_EQ(gSpecialVar_Result, PARTY_SIZE);
}

TEST("CanUseFly requires the badge")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_TAILLOW, 5;
    );
    EXPECT_EQ(CanUseFly(), FALSE);
    UnlockFieldMove(FIELD_MOVE_FLY);
    EXPECT_EQ(CanUseFly(), TRUE);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_POKEMON);
}

TEST("CanUseFly accepts the Fly Tool when no party mon can learn Fly")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLY);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
    );
    EXPECT_EQ(CanUseFly(), FALSE);
    AddBagItem(ITEM_FLY_TOOL, 1);
    EXPECT_EQ(CanUseFly(), TRUE);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_ITEM);
}

TEST("CanUseFly skips eggs that could otherwise learn Fly")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLY);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_TAILLOW, 5;
    );
    MakeSlotAnEgg(0);
    EXPECT_EQ(CanUseFly(), FALSE);
    AddBagItem(ITEM_FLY_TOOL, 1);
    EXPECT_EQ(CanUseFly(), TRUE);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_ITEM);
}

TEST("CanUseFlash requires the badge")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ABRA, 5;
    );
    EXPECT_EQ(CanUseFlash(), FALSE);
    UnlockFieldMove(FIELD_MOVE_FLASH);
    EXPECT_EQ(CanUseFlash(), TRUE);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_POKEMON);
}

TEST("CanUseFlash accepts the Flash Tool when no party mon can learn Flash")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLASH);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_WOBBUFFET, 5;
    );
    EXPECT_EQ(CanUseFlash(), FALSE);
    AddBagItem(ITEM_FLASH_TOOL, 1);
    EXPECT_EQ(CanUseFlash(), TRUE);
    EXPECT_EQ(GetFieldMoveSource(), FIELD_MOVE_SOURCE_ITEM);
}

TEST("Every field move has a setup function and a move id")
{
    for (u32 i = 0; i < FIELD_MOVES_COUNT; i++)
    {
        EXPECT(gFieldMoveInfo[i].fieldMoveFunc != NULL);
        EXPECT_NE(FieldMove_GetMoveId(i), MOVE_NONE);
    }
}

#if !IS_FRLG
TEST("Cut is unlocked by the first badge")
{
    EXPECT_EQ(IsFieldMoveUnlocked(FIELD_MOVE_CUT), FALSE);
    FlagSet(FLAG_BADGE01_GET);
    EXPECT_EQ(IsFieldMoveUnlocked(FIELD_MOVE_CUT), TRUE);
}
#endif

#if !OW_ROCK_CLIMB_FIELD_MOVE
TEST("Rock Climb is hidden while OW_ROCK_CLIMB_FIELD_MOVE is off")
{
    EXPECT_EQ(IsFieldMoveUnlocked(FIELD_MOVE_ROCK_CLIMB), FALSE);
    EXPECT_EQ(FieldMove_IsVisible(FIELD_MOVE_ROCK_CLIMB), FALSE);
}
#endif

#if !OW_DEFOG_FIELD_MOVE
TEST("Defog is hidden while OW_DEFOG_FIELD_MOVE is off")
{
    EXPECT_EQ(IsFieldMoveUnlocked(FIELD_MOVE_DEFOG), FALSE);
    EXPECT_EQ(FieldMove_IsVisible(FIELD_MOVE_DEFOG), FALSE);
}
#endif

static bool32 PartyMenuListsFieldMove(u32 slotId, enum FieldMove fieldMove)
{
    u8 fieldMoves[8];
    u32 count = Test_GetPartyMenuFieldMoves(gParties[B_TRAINER_PLAYER], slotId, fieldMoves);

    for (u32 i = 0; i < count; i++)
    {
        if (fieldMoves[i] == fieldMove)
            return TRUE;
    }
    return FALSE;
}

TEST("Party menu lists Fly for a mon that can learn it once the badge is obtained")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_TAILLOW, 5;
    );
    EXPECT_EQ(PartyMenuListsFieldMove(0, FIELD_MOVE_FLY), FALSE);
    UnlockFieldMove(FIELD_MOVE_FLY);
    EXPECT_EQ(PartyMenuListsFieldMove(0, FIELD_MOVE_FLY), TRUE);
}

TEST("Party menu does not list Fly for a mon that cannot learn it")
{
    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLY);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
    );
    EXPECT_EQ(PartyMenuListsFieldMove(0, FIELD_MOVE_FLY), FALSE);
}

TEST("Party menu never lists field moves that have a key item")
{
    u32 i;

    PARAMETRIZE { i = FIELD_MOVE_CUT; }
    PARAMETRIZE { i = FIELD_MOVE_SURF; }
    PARAMETRIZE { i = FIELD_MOVE_STRENGTH; }
    PARAMETRIZE { i = FIELD_MOVE_ROCK_SMASH; }
    PARAMETRIZE { i = FIELD_MOVE_DIVE; }
    PARAMETRIZE { i = FIELD_MOVE_WATERFALL; }

    ZeroPlayerPartyMons();
    UnlockFieldMove(i);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
    );
    TeachSlotMove(0, FieldMove_GetMoveId(i));
    EXPECT_EQ(PartyMenuListsFieldMove(0, i), FALSE);
}

TEST("Party menu lists Dig only when the mon knows it")
{
    ZeroPlayerPartyMons();
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_ZIGZAGOON, 5;
    );
    EXPECT_EQ(PartyMenuListsFieldMove(0, FIELD_MOVE_DIG), FALSE);
    TeachSlotMove(0, MOVE_DIG);
    EXPECT_EQ(PartyMenuListsFieldMove(0, FIELD_MOVE_DIG), TRUE);
}

TEST("Party menu lists Fly once for a mon that already knows it")
{
    u8 fieldMoves[8];
    u32 count;

    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLY);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_TAILLOW, 5;
    );
    TeachSlotMove(0, MOVE_FLY);
    count = Test_GetPartyMenuFieldMoves(gParties[B_TRAINER_PLAYER], 0, fieldMoves);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(fieldMoves[0], FIELD_MOVE_FLY);
}

TEST("Party menu lists no field moves for an egg")
{
    u8 fieldMoves[8];

    ZeroPlayerPartyMons();
    UnlockFieldMove(FIELD_MOVE_FLY);
    RUN_OVERWORLD_SCRIPT(
        givemon SPECIES_TAILLOW, 5;
    );
    TeachSlotMove(0, MOVE_DIG);
    MakeSlotAnEgg(0);
    EXPECT_EQ(Test_GetPartyMenuFieldMoves(gParties[B_TRAINER_PLAYER], 0, fieldMoves), 0);
}
