#ifndef GUARD_FIELD_MOVE_TOOLS_H
#define GUARD_FIELD_MOVE_TOOLS_H

#include "global.h"
#include "config/field_move_tools.h"
#include "constants/field_move.h"

// How the party menu decides whether to list a field move for a Pokémon.
enum FieldMovePartyMenuPolicy
{
    FIELD_MOVE_MENU_KNOWN,     // Vanilla: listed when the Pokémon knows the move.
    FIELD_MOVE_MENU_LEARNABLE, // Listed when the Pokémon could learn the move.
    FIELD_MOVE_MENU_TOOL_ONLY, // Never listed, the tool replaces the party menu entry.
};

struct FieldMoveTool
{
    u16 keyItem; // The tool that stands in for the move, ITEM_NONE if it has none.
    enum FieldMovePartyMenuPolicy partyMenuPolicy;
};

extern const struct FieldMoveTool gFieldMoveTools[FIELD_MOVES_COUNT];

u16 FieldMoveTool_GetKeyItem(enum FieldMove fieldMove);
bool32 FieldMoveTool_HasKeyItem(enum FieldMove fieldMove);
u32 FieldMoveTool_GetPartyMenuFieldMoves(struct Pokemon *mon, u8 *fieldMoves);

#endif // GUARD_FIELD_MOVE_TOOLS_H
