#ifndef GUARD_CONFIG_FIELD_MOVE_TOOLS_H
#define GUARD_CONFIG_FIELD_MOVE_TOOLS_H

// Key items that stand in for the HM-style field moves (Cut Tool, Surf Tool, and so on),
// so the player does not have to carry a Pokémon that knows them.
//
// TRUE:  'checkfieldmove' falls back to a tool when no party Pokémon can use the move, the
//        moves the tools replace are dropped from the party menu, and Fly and Flash are
//        listed for any Pokémon that could learn them.
// FALSE: field move lookups behave as they do upstream - no item fallback, and the party
//        menu lists a field move only when the Pokémon knows it.
//
// The toggle covers what looks for the tools, not the tools themselves. The eight items keep
// their field use functions either way, so a build with this off should simply never hand
// them to the player.
#define OW_FIELD_MOVE_TOOLS             TRUE

#endif // GUARD_CONFIG_FIELD_MOVE_TOOLS_H
