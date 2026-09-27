#include "global.h"
#include "field_move.h"
#include "field_move_tools.h"
#include "item.h"
#include "party_menu.h"
#include "pokemon.h"
#include "constants/field_move.h"
#include "constants/items.h"

// Which key item stands in for each field move, and how the party menu lists it.
// Field moves left out of this table have no tool and keep vanilla behaviour, because
// ITEM_NONE and FIELD_MOVE_MENU_KNOWN are both zero.
const struct FieldMoveTool gFieldMoveTools[FIELD_MOVES_COUNT] =
{
    [FIELD_MOVE_CUT] =
    {
        .keyItem = ITEM_CUT_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
    [FIELD_MOVE_FLASH] =
    {
        .keyItem = ITEM_FLASH_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_LEARNABLE,
    },
    [FIELD_MOVE_ROCK_SMASH] =
    {
        .keyItem = ITEM_ROCK_SMASH_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
    [FIELD_MOVE_STRENGTH] =
    {
        .keyItem = ITEM_STRENGTH_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
    [FIELD_MOVE_SURF] =
    {
        .keyItem = ITEM_SURF_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
    [FIELD_MOVE_FLY] =
    {
        .keyItem = ITEM_FLY_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_LEARNABLE,
    },
    [FIELD_MOVE_DIVE] =
    {
        .keyItem = ITEM_DIVE_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
    [FIELD_MOVE_WATERFALL] =
    {
        .keyItem = ITEM_WATERFALL_TOOL,
        .partyMenuPolicy = FIELD_MOVE_MENU_TOOL_ONLY,
    },
};

u16 FieldMoveTool_GetKeyItem(enum FieldMove fieldMove)
{
    if (!OW_FIELD_MOVE_TOOLS)
        return ITEM_NONE;

    return gFieldMoveTools[fieldMove].keyItem;
}

// Whether the bag holds the tool that stands in for 'fieldMove'.
bool32 FieldMoveTool_HasKeyItem(enum FieldMove fieldMove)
{
    u16 keyItem = FieldMoveTool_GetKeyItem(fieldMove);

    return keyItem != ITEM_NONE && CheckBagHasItem(keyItem, 1);
}

static enum FieldMovePartyMenuPolicy GetPartyMenuPolicy(enum FieldMove fieldMove)
{
    if (!OW_FIELD_MOVE_TOOLS)
        return FIELD_MOVE_MENU_KNOWN;

    return gFieldMoveTools[fieldMove].partyMenuPolicy;
}

// Writes the field moves the party menu should list for 'mon' to 'fieldMoves' as
// 'enum FieldMove' values, and returns how many were written.
//
// Moves the Pokémon knows come first, in move slot order, the way they do upstream.
// Moves listed from the learnset (Fly and Flash) follow, skipping any the Pokémon
// already knows so they cannot be listed twice.
//
// At most MAX_MON_MOVES moves are reported, which is also the most the upstream menu can
// list, so the caller's action list cannot overflow.
u32 FieldMoveTool_GetPartyMenuFieldMoves(struct Pokemon *mon, u8 *fieldMoves)
{
    u32 count = 0;

    // Eggs are routed to ACTIONS_SWITCH before the menu gets here, so this only matters
    // to callers that build the list directly.
    if (GetMonData(mon, MON_DATA_IS_EGG))
        return 0;

    for (u32 slot = 0; slot < MAX_MON_MOVES && count < MAX_MON_MOVES; slot++)
    {
        for (u32 i = 0; i < FIELD_MOVES_COUNT; i++)
        {
            if (!FieldMove_IsVisible(i) || GetPartyMenuPolicy(i) == FIELD_MOVE_MENU_TOOL_ONLY)
                continue;

            if (GetMonData(mon, MON_DATA_MOVE1 + slot) == FieldMove_GetMoveId(i))
            {
                fieldMoves[count++] = i;
                break;
            }
        }
    }

    for (u32 i = 0; i < FIELD_MOVES_COUNT && count < MAX_MON_MOVES; i++)
    {
        if (GetPartyMenuPolicy(i) != FIELD_MOVE_MENU_LEARNABLE)
            continue;
        if (!FieldMove_IsVisible(i) || !IsFieldMoveUnlocked(i))
            continue;
        if (MonKnowsMove(mon, FieldMove_GetMoveId(i)))
            continue;
        if (!CanLearnTeachableMove(GetMonData(mon, MON_DATA_SPECIES), FieldMove_GetMoveId(i)))
            continue;

        fieldMoves[count++] = i;
    }

    return count;
}
