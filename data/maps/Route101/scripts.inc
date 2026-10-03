Route101_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, Route101_OnTransition
	map_script MAP_SCRIPT_ON_FRAME_TABLE, Route101_OnFrame
	.byte 0

Route101_OnTransition:
	call ProfBirch_EventScript_UpdateLocation
	end

Route101_OnFrame:
	map_script_2 VAR_ROUTE101_STATE, 0, Route101_EventScript_HideMapNamePopup
	.2byte 0

Route101_EventScript_HideMapNamePopup::
	setflag FLAG_HIDE_MAP_NAME_POPUP
	setvar VAR_ROUTE101_STATE, 1
	end

Route101_EventScript_StartBirchRescue::
	lockall
	playbgm MUS_HELP, TRUE
	msgbox Route101_Text_HelpMe, MSGBOX_DEFAULT
	closemessage
	setobjectxy LOCALID_ROUTE101_BIRCH, 0, 15
	setobjectxy LOCALID_ROUTE101_ZIGZAGOON, 0, 16
	applymovement LOCALID_PLAYER, Route101_Movement_EnterScene
	applymovement LOCALID_ROUTE101_BIRCH, Route101_Movement_BirchRunAway1
	applymovement LOCALID_ROUTE101_ZIGZAGOON, Route101_Movement_ZigzagoonChase1
	waitmovement 0
	applymovement LOCALID_ROUTE101_ZIGZAGOON, Route101_Movement_ZigzagoonChaseInCircles
	applymovement LOCALID_ROUTE101_BIRCH, Route101_Movement_BirchRunInCircles
	waitmovement 0
	applymovement LOCALID_ROUTE101_BIRCH, Common_Movement_WalkInPlaceFasterRight
	waitmovement 0
	applymovement LOCALID_ROUTE101_ZIGZAGOON, Route101_Movement_ZigzagoonFaceBirch
	applymovement LOCALID_ROUTE101_BIRCH, Route101_Movement_BirchFaceZigzagoon
	waitmovement 0
	msgbox Route101_Text_PleaseHelp, MSGBOX_DEFAULT
	closemessage
	setvar VAR_ROUTE101_STATE, 2
	releaseall
	end

Route101_EventScript_PreventExitSouth::
	lockall
	msgbox Route101_Text_DontLeaveMe, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, Route101_Movement_PreventExitSouth
	waitmovement 0
	releaseall
	end

Route101_EventScript_PreventExitWest::
	lockall
	msgbox Route101_Text_DontLeaveMe, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, Route101_Movement_PreventExitWest
	waitmovement 0
	releaseall
	end

Route101_EventScript_DexNavTutorial0::
	setvar VAR_0x8004, 0
	goto Route101_EventScript_DexNavTutorial_Common

Route101_EventScript_DexNavTutorial1::
	setvar VAR_0x8004, 1
	goto Route101_EventScript_DexNavTutorial_Common

Route101_EventScript_DexNavTutorial2::
	setvar VAR_0x8004, 2
	goto Route101_EventScript_DexNavTutorial_Common


Route101_EventScript_DexNavTutorial_Common::
	lockall
	goto_if_set FLAG_DEXNAV_TUTORIAL_DONE, Route101_EventScript_DexNavTutorialEnd
	goto_if_unset FLAG_RECEIVED_POKEDEX_FROM_BIRCH, Route101_EventScript_DexNavTutorialEnd
	checkplayergender
	goto_if_eq VAR_RESULT, MALE, Route101_EventScript_DexNavTutorial_May
	goto Route101_EventScript_DexNavTutorial_Brendan

Route101_EventScript_DexNavTutorial_May::
	setvar VAR_OBJ_GFX_ID_0, OBJ_EVENT_GFX_MAY_NORMAL
	playbgm MUS_ENCOUNTER_MAY, TRUE
	goto Route101_EventScript_DexNavTutorial_StartCommon

Route101_EventScript_DexNavTutorial_Brendan::
	setvar VAR_OBJ_GFX_ID_0, OBJ_EVENT_GFX_BRENDAN_NORMAL
	playbgm MUS_ENCOUNTER_BRENDAN, TRUE
	goto Route101_EventScript_DexNavTutorial_StartCommon

Route101_EventScript_DexNavTutorial_StartCommon::
	clearflag FLAG_HIDE_ROUTE101_RIVAL
	addobject LOCALID_ROUTE101_RIVAL
	switch VAR_0x8004
	case 0, Route101_EventScript_DexNavTutorial_Start0
	case 1, Route101_EventScript_DexNavTutorial_Start1
	case 2, Route101_EventScript_DexNavTutorial_Start2

Route101_EventScript_DexNavTutorial_Start0::
	setobjectxy LOCALID_ROUTE101_RIVAL, 10, 17
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalApproach0
	goto Route101_EventScript_DexNavTutorial_AfterApproach

Route101_EventScript_DexNavTutorial_Start1::
	setobjectxy LOCALID_ROUTE101_RIVAL, 10, 17
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalApproach1
	goto Route101_EventScript_DexNavTutorial_AfterApproach

Route101_EventScript_DexNavTutorial_Start2::
	setobjectxy LOCALID_ROUTE101_RIVAL, 10, 17
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalApproach2
	goto Route101_EventScript_DexNavTutorial_AfterApproach

Route101_EventScript_DexNavTutorial_AfterApproach::
	waitmovement 0
	applymovement LOCALID_PLAYER, Common_Movement_WalkInPlaceFasterLeft
	waitmovement 0

	msgbox Route101_Text_RivalCatchUp, MSGBOX_DEFAULT
	closemessage
	msgbox Route101_Text_RivalDexNavExplain, MSGBOX_DEFAULT
	playfanfare MUS_OBTAIN_ITEM
	msgbox Route101_Text_RivalDexNavReceived, MSGBOX_DEFAULT
	waitfanfare
	setflag FLAG_DEXNAV_GET
	setflag FLAG_DETECTOR_MODE

	msgbox Route101_Text_RivalSneakExplain, MSGBOX_DEFAULT
	msgbox Route101_Text_RivalSneakExplain2, MSGBOX_DEFAULT

	applymovement LOCALID_ROUTE101_RIVAL, Common_Movement_ExclamationMark
	waitmovement 0
	playse SE_PIN
	msgbox Route101_Text_RivalSpotsPoochyena, MSGBOX_DEFAULT
	closemessage

	setwildbattle SPECIES_POOCHYENA, 5, ITEM_NONE
	special ModifyDexNavTutorialPoochyena
	dowildbattle

	msgbox Route101_Text_RivalAfterTutorial, MSGBOX_DEFAULT
	closemessage
	switch VAR_0x8004
	case 0, Route101_EventScript_DexNavTutorial_Leave0
	case 1, Route101_EventScript_DexNavTutorial_Leave1
	case 2, Route101_EventScript_DexNavTutorial_Leave2

Route101_EventScript_DexNavTutorial_Leave0::
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalLeave0
	goto Route101_EventScript_DexNavTutorial_Finish

Route101_EventScript_DexNavTutorial_Leave1::
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalLeave1
	goto Route101_EventScript_DexNavTutorial_Finish

Route101_EventScript_DexNavTutorial_Leave2::
	applymovement LOCALID_ROUTE101_RIVAL, Route101_Movement_RivalLeave2
	goto Route101_EventScript_DexNavTutorial_Finish

Route101_EventScript_DexNavTutorial_Finish::
	waitmovement 0
	removeobject LOCALID_ROUTE101_RIVAL
	setflag FLAG_HIDE_ROUTE101_RIVAL
	setflag FLAG_DEXNAV_TUTORIAL_DONE
	fadedefaultbgm
	releaseall
	end

Route101_EventScript_DexNavTutorialEnd::
	releaseall
	end

Route101_Movement_RivalApproach0:
	walk_fast_up
	walk_fast_up
	walk_fast_up
	jump_2_up
	walk_fast_right
	step_end

Route101_Movement_RivalLeave0:
	walk_fast_left
	jump_2_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	step_end

Route101_Movement_RivalApproach1:
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_fast_up
	jump_2_up
	walk_fast_right
	step_end

Route101_Movement_RivalLeave1:
	walk_fast_left
	walk_fast_down
	jump_2_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	step_end

Route101_Movement_RivalApproach2:
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_fast_up
	jump_2_up
	walk_fast_right
	step_end

Route101_Movement_RivalLeave2:
	walk_fast_left
	walk_fast_down
	walk_fast_down
	jump_2_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	step_end

Route101_Movement_PreventExitSouth:
	walk_up
	step_end

Route101_Movement_PreventExitWest:
	walk_right
	step_end

Route101_Movement_ZigzagoonChaseInCircles:
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	walk_fast_left
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	walk_fast_left
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	step_end

Route101_Movement_ZigzagoonChase1:
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_up
	step_end

@ Leftover data? This command is unused.
	step_end

Route101_Movement_ZigzagoonFaceBirch:
	walk_in_place_fast_left
	walk_in_place_fast_left
	walk_in_place_fast_left
	walk_in_place_fast_left
	step_end

Route101_Movement_EnterScene:
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_fast_up
	walk_in_place_faster_left
	step_end

Route101_Movement_BirchRunInCircles:
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	walk_fast_left
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	walk_fast_left
	walk_fast_up
	walk_fast_up
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_down
	walk_fast_down
	walk_fast_left
	walk_fast_left
	walk_fast_left
	step_end

Route101_Movement_BirchRunAway1:
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_right
	walk_fast_up
	walk_fast_up
	step_end

@ Leftover data? This command is unused.
	step_end

Route101_Movement_BirchFaceZigzagoon:
	walk_in_place_fast_right
	walk_in_place_fast_right
	walk_in_place_fast_right
	walk_in_place_fast_right
	step_end

Route101_Movement_Unused1:
	walk_up
	walk_up
	step_end

Route101_Movement_Unused2:
	walk_up
	walk_left
	walk_up
	step_end

Route101_EventScript_Youngster::
	msgbox Route101_Text_TakeTiredPokemonToPokeCenter, MSGBOX_NPC
	end

Route101_EventScript_Boy::
	msgbox Route101_Text_WildPokemonInTallGrass, MSGBOX_NPC
	end

Route101_EventScript_RouteSign::
	msgbox Route101_Text_RouteSign, MSGBOX_SIGN
	end

Route101_EventScript_BirchsBag::
	lock
	faceplayer
	setflag FLAG_SYS_POKEMON_GET
	setflag FLAG_RESCUED_BIRCH
	fadescreen FADE_TO_BLACK
	removeobject LOCALID_ROUTE101_ZIGZAGOON
	setobjectxy LOCALID_PLAYER, 6, 13
	applymovement LOCALID_PLAYER, Common_Movement_WalkInPlaceFasterLeft
	waitmovement 0
	special ChooseStarter
	applymovement LOCALID_ROUTE101_BIRCH, Route101_Movement_BirchApproachPlayer
	waitmovement 0
	msgbox Route101_Text_YouSavedMe, MSGBOX_DEFAULT
	special HealPlayerParty
	setflag FLAG_HIDE_ROUTE_101_BIRCH_ZIGZAGOON_BATTLE
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_BIRCH
	setflag FLAG_HIDE_ROUTE_101_BIRCH_STARTERS_BAG
	setvar VAR_BIRCH_LAB_STATE, 2
	setvar VAR_ROUTE101_STATE, 3
	clearflag FLAG_HIDE_MAP_NAME_POPUP
	checkplayergender
	call_if_eq VAR_RESULT, MALE, Route101_EventScript_HideMayInBedroom
	call_if_eq VAR_RESULT, FEMALE, Route101_EventScript_HideBrendanInBedroom
	warp MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB, 6, 5
	waitstate
	release
	end

Route101_EventScript_HideMayInBedroom::
	setflag FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM
	return

Route101_EventScript_HideBrendanInBedroom::
	setflag FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_BEDROOM
	return

Route101_Movement_BirchApproachPlayer:
	walk_right
	step_end

Route101_Text_HelpMe:
    .string "救、救命——！！$"

Route101_Text_PleaseHelp:
    .string "喂！那边的你！\n拜托！救救我！\p在那边的包里面\n里面有精灵球！$"

Route101_Text_DontLeaveMe:
    .string "你、你要去哪儿？！\n别丢下我啊——！$"

Route101_Text_YouSavedMe:
    .string "小田卷博士：哈啊哈啊……\p我刚想在草丛做调查，\n就被野生的宝可梦袭击了……\p总之得救了，\n谢谢你！\p咦？\p你不是{PLAYER}{KUN}吗？\p这儿不是聊天的地方，\n跟我来宝可梦研究所吧？$"

Route101_Text_TakeTiredPokemonToPokeCenter:
    .string "宝可梦累了的话，\n就要带它们去宝可梦中心。\p附近的古玫镇里\n就有个宝可梦中心。$"

Route101_Text_WildPokemonInTallGrass:
    .string "在草丛中行走时野生宝可梦\n有可能跳出来袭击。\p如果想捕捉宝可梦的话，\n就得去草丛里寻找。\p有些宝可梦可能\n在夜晚才会遇到。$"

Route101_Text_RouteSign:
    .string "101号道路\n{UP_ARROW}古玫镇$"

Route101_Text_RivalCatchUp:
	.string "{PLAYER}！等一下——！$"

Route101_Text_RivalDexNavExplain:
	.string "差点忘了把这个给你。\p"
	.string "这是宝可梦多功能导航器的\n"
	.string "新应用——宝可梦搜寻器。\p"
	.string "它可以告诉你当前区域\n"
	.string "有哪些宝可梦出没。$"

Route101_Text_RivalDexNavReceived:
	.string "{PLAYER}的宝可梦多功能导航器\n"
	.string "安装了宝可梦搜寻器！$"

Route101_Text_RivalSneakExplain:
	.string "对了，再教你一个技巧。\p"
	.string "按住A键行动,就能悄悄 \n"
	.string "靠近草丛里的宝可梦。$"

Route101_Text_RivalSneakExplain2:
	.string "如果草丛里出现了\n"
	.string "摇晃，就说明那里\l"
	.string "有宝可梦。\p"
	.string "这时候慢慢靠近，\n"
	.string "就能遇到它！$"

Route101_Text_RivalSpotsPoochyena:
	.string "看！那边草丛里有动静！\n"
	.string "去试试吧！$"

Route101_Text_RivalAfterTutorial:
	.string "干得漂亮！\p"
	.string "宝可梦搜寻器应该已经\n"
	.string "自动记录下来了。\p"
	.string "我还有事，先走了。\n"
	.string "以后再见！$"
