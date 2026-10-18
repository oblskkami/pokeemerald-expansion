Route103_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, Route103_OnTransition
	map_script MAP_SCRIPT_ON_LOAD, Route103_OnLoad
	.byte 0

Route103_OnTransition:
	call Common_EventScript_SetupRivalGfxId
	call ProfBirch_EventScript_UpdateLocation
	call_if_unset FLAG_DEFEATED_RIVAL_ROUTE103, Route103_EventScript_ResetRival
	call_if_unset FLAG_DEFEATED_RIVAL_ROUTE103, Route103_EventScript_HideSeviper
	end

Route103_EventScript_ResetRival::
	destroyfollowernpc
	clearflag FLAG_HIDE_ROUTE_103_RIVAL
	addobject LOCALID_ROUTE103_RIVAL
	return

Route103_EventScript_HideSeviper::
	setflag FLAG_HIDE_ROUTE103_SEVIPER
	return

Route103_OnLoad:
	call_if_set FLAG_SYS_GAME_CLEAR, Route103_EventScript_OpenAlteringCave
	end

Route103_EventScript_OpenAlteringCave::
	setmetatile 45, 5, METATILE_General_CaveEntrance_Top, TRUE
	setmetatile 45, 6, METATILE_General_CaveEntrance_Bottom, FALSE
	return

Route103_EventScript_Rival::
	lockall
	goto_if_set FLAG_ROUTE103_RIVAL_TAG_RETRY, Route103_EventScript_RivalRetryTagBattle
	checkplayergender
	goto_if_eq VAR_RESULT, MALE, Route103_EventScript_RivalMay
	goto_if_eq VAR_RESULT, FEMALE, Route103_EventScript_RivalBrendan
	end

Route103_EventScript_RivalRetryTagBattle::
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_FacePlayer
	waitmovement 0
	msgbox Route103_Text_RivalRetryAmbush, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_ExclamationMark
	waitmovement 0
	applymovement LOCALID_PLAYER, Common_Movement_WalkInPlaceFasterUp
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_WalkInPlaceFasterUp
	waitmovement 0
	clearflag FLAG_HIDE_ROUTE103_SEVIPER
	addobject LOCALID_ROUTE103_SEVIPER
	playmoncry SPECIES_SEVIPER, 0
	delay 30
	switch VAR_STARTER_MON
	case 0, Route103_EventScript_Retry_SetPartnerMayTreecko
	case 1, Route103_EventScript_Retry_SetPartnerMayTorchic
	case 2, Route103_EventScript_Retry_SetPartnerMayMudkip
	end

Route103_EventScript_Retry_SetPartnerMayTreecko::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_TREECKO
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_Retry_SetPartnerMayTorchic::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_TORCHIC
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_Retry_SetPartnerMayMudkip::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_MUDKIP
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_Retry_SetPartnerBrendanTreecko::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_TREECKO
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_Retry_SetPartnerBrendanTorchic::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_TORCHIC
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_Retry_SetPartnerBrendanMudkip::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_MUDKIP
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_RivalMay::
	msgbox Route103_Text_MayRoute103Pokemon, MSGBOX_DEFAULT
	closemessage
	playbgm MUS_ENCOUNTER_MAY, TRUE
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_FacePlayer
	waitmovement 0
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_ExclamationMark
	waitmovement 0
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_Delay48
	waitmovement 0
	msgbox Route103_Text_MayLetsBattle, MSGBOX_DEFAULT
	switch VAR_STARTER_MON
	case 0, Route103_EventScript_StartMayBattleTreecko
	case 1, Route103_EventScript_StartMayBattleTorchic
	case 2, Route103_EventScript_StartMayBattleMudkip
	end

Route103_EventScript_RivalBrendan::
	msgbox Route103_Text_BrendanRoute103Pokemon, MSGBOX_DEFAULT
	closemessage
	playbgm MUS_ENCOUNTER_BRENDAN, TRUE
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_FacePlayer
	waitmovement 0
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_ExclamationMark
	waitmovement 0
	applymovement LOCALID_ROUTE103_RIVAL, Common_Movement_Delay48
	waitmovement 0
	msgbox Route103_Text_BrendanLetsBattle, MSGBOX_DEFAULT
	switch VAR_STARTER_MON
	case 0, Route103_EventScript_StartBrendanBattleTreecko
	case 1, Route103_EventScript_StartBrendanBattleTorchic
	case 2, Route103_EventScript_StartBrendanBattleMudkip
	end

Route103_EventScript_StartMayBattleTreecko::
	trainerbattle_no_intro TRAINER_MAY_ROUTE_103_TREECKO, Route103_Text_MayDefeated
	goto Route103_EventScript_AfterMayBattle
	end

Route103_EventScript_StartMayBattleTorchic::
	trainerbattle_no_intro TRAINER_MAY_ROUTE_103_TORCHIC, Route103_Text_MayDefeated
	goto Route103_EventScript_AfterMayBattle
	end

Route103_EventScript_StartMayBattleMudkip::
	trainerbattle_no_intro TRAINER_MAY_ROUTE_103_MUDKIP, Route103_Text_MayDefeated
	goto Route103_EventScript_AfterMayBattle
	end

Route103_EventScript_StartBrendanBattleTreecko::
	trainerbattle_no_intro TRAINER_BRENDAN_ROUTE_103_TREECKO, Route103_Text_BrendanDefeated
	goto Route103_EventScript_AfterBrendanBattle
	end

Route103_EventScript_StartBrendanBattleTorchic::
	trainerbattle_no_intro TRAINER_BRENDAN_ROUTE_103_TORCHIC, Route103_Text_BrendanDefeated
	goto Route103_EventScript_AfterBrendanBattle
	end

Route103_EventScript_StartBrendanBattleMudkip::
	trainerbattle_no_intro TRAINER_BRENDAN_ROUTE_103_MUDKIP, Route103_Text_BrendanDefeated
	goto Route103_EventScript_AfterBrendanBattle
	end

Route103_EventScript_AfterMayBattle::
	setflag FLAG_ROUTE103_RIVAL_TAG_RETRY
	msgbox Route103_Text_MayTimeToHeadBack, MSGBOX_DEFAULT
	closemessage
	clearflag FLAG_HIDE_ROUTE_103_RIVAL
	addobject LOCALID_ROUTE103_RIVAL
	switch VAR_STARTER_MON
	case 0, Route103_EventScript_SetPartnerMayTreecko
	case 1, Route103_EventScript_SetPartnerMayTorchic
	case 2, Route103_EventScript_SetPartnerMayMudkip
	end

Route103_EventScript_SetPartnerMayTreecko::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_TREECKO
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_SetPartnerMayTorchic::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_TORCHIC
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_SetPartnerMayMudkip::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_MAY_ROUTE_103_MUDKIP
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_AfterBrendanBattle::
	setflag FLAG_ROUTE103_RIVAL_TAG_RETRY
	msgbox Route103_Text_BrendanTimeToHeadBack, MSGBOX_DEFAULT
	closemessage
	clearflag FLAG_HIDE_ROUTE_103_RIVAL
	addobject LOCALID_ROUTE103_RIVAL
	switch VAR_STARTER_MON
	case 0, Route103_EventScript_SetPartnerBrendanTreecko
	case 1, Route103_EventScript_SetPartnerBrendanTorchic
	case 2, Route103_EventScript_SetPartnerBrendanMudkip
	end

Route103_EventScript_SetPartnerBrendanTreecko::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_TREECKO
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_SetPartnerBrendanTorchic::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_TORCHIC
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_SetPartnerBrendanMudkip::
	setfollowernpc LOCALID_ROUTE103_RIVAL, FNPC_ALL, 0, PARTNER_BRENDAN_ROUTE_103_MUDKIP
	goto Route103_EventScript_TagBattle_Ambush
	end

Route103_EventScript_TagBattle_Ambush::
	applymovement LOCALID_PLAYER, Common_Movement_WalkInPlaceFasterUp
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Common_Movement_WalkInPlaceFasterUp
	waitmovement 0
	clearflag FLAG_HIDE_ROUTE103_SEVIPER
	addobject LOCALID_ROUTE103_SEVIPER
	playmoncry SPECIES_SEVIPER, 0
	delay 30
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Common_Movement_ExclamationMark
	waitmovement 0
	msgbox Route103_Text_SeviperAmbush, MSGBOX_DEFAULT
	closemessage
	goto Route103_EventScript_SeviperBattle_Start
	end

Route103_EventScript_SeviperBattle_Start::
	setwildbattle SPECIES_SEVIPER, 7
	dowildbattle
	removeobject LOCALID_ROUTE103_SEVIPER
	special HealPlayerParty
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Common_Movement_FacePlayer
	waitmovement 0
	msgbox Route103_Text_RivalPromise, MSGBOX_DEFAULT
	closemessage
	goto Route103_EventScript_RivalExit
	end

Route103_EventScript_RivalExit::
	closemessage
	switch VAR_FACING
	case DIR_SOUTH, Route103_EventScript_RivalExitFacingSouth
	case DIR_NORTH, Route103_EventScript_RivalExitFacingNorth
	case DIR_WEST, Route103_EventScript_RivalExitFacingEastOrWest
	case DIR_EAST, Route103_EventScript_RivalExitFacingEastOrWest
	end

Route103_EventScript_RivalExitFacingNorth::
	applymovement LOCALID_PLAYER, Route103_Movement_WatchRivalExitFacingNorth
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExitFacingNorth1
	waitmovement 0
	playse SE_LEDGE
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExitFacingNorth2
	waitmovement 0
	goto Route103_EventScript_RivalEnd
	end

Route103_EventScript_RivalExitFacingEastOrWest::
	applymovement LOCALID_PLAYER, Route103_Movement_WatchRivalExitFacingEastOrWest
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExit1
	waitmovement 0
	playse SE_LEDGE
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExit2
	waitmovement 0
	goto Route103_EventScript_RivalEnd
	end

Route103_EventScript_RivalExitFacingSouth::
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExit1
	waitmovement 0
	playse SE_LEDGE
	applymovement OBJ_EVENT_ID_NPC_FOLLOWER, Route103_Movement_RivalExit2
	waitmovement 0
	goto Route103_EventScript_RivalEnd
	end

Route103_EventScript_RivalEnd::
	destroyfollowernpc
	removeobject LOCALID_ROUTE103_RIVAL
	setflag FLAG_HIDE_ROUTE_103_RIVAL
	clearflag FLAG_ROUTE103_RIVAL_TAG_RETRY
	setvar VAR_BIRCH_LAB_STATE, 4
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_RIVAL
	setflag FLAG_DEFEATED_RIVAL_ROUTE103
	setvar VAR_OLDALE_RIVAL_STATE, 1
	clearflag FLAG_HIDE_OLDALE_TOWN_RIVAL
	savebgm MUS_DUMMY
	fadedefaultbgm
	releaseall
	end

Route103_Movement_RivalExitFacingNorth1:
	walk_left
	walk_down
	step_end

Route103_Movement_RivalExitFacingNorth2:
	jump_2_down
	delay_16
	walk_down
	walk_down
	walk_down
	walk_down
	step_end

Route103_Movement_WatchRivalExitFacingNorth:
	delay_16
	delay_4
	walk_in_place_faster_left
	delay_16
	walk_in_place_faster_down
	step_end

Route103_Movement_RivalExit1:
	walk_down
	step_end

Route103_Movement_RivalExit2:
	jump_2_down
	delay_16
	walk_down
	walk_down
	walk_down
	step_end

Route103_Movement_WatchRivalExitFacingEastOrWest:
	delay_16
	walk_in_place_faster_down
	step_end

Route103_EventScript_Boy::
	msgbox Route103_Text_ShouldHaveBroughtPotion, MSGBOX_NPC
	end

Route103_EventScript_Man::
	msgbox Route103_Text_ShortcutToOldale, MSGBOX_NPC
	end

Route103_EventScript_RouteSign::
	msgbox Route103_Text_RouteSign, MSGBOX_SIGN
	end

Route103_EventScript_Daisy::
	trainerbattle_single TRAINER_DAISY, Route103_Text_DaisyIntro, Route103_Text_DaisyDefeated
	msgbox Route103_Text_DaisyPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Amy::
	trainerbattle_double TRAINER_AMY_AND_LIV_1, Route103_Text_AmyIntro, Route103_Text_AmyDefeated, Route103_Text_AmyNotEnoughPokemon, Route102_EventScript_AmyRegisterMatchCallAfterBattle
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route102_EventScript_AmyRematch
	msgbox Route103_Text_AmyPostBattle, MSGBOX_AUTOCLOSE
	end

Route102_EventScript_AmyRegisterMatchCallAfterBattle::
	msgbox Route103_Text_AmyLivRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_AMY_AND_LIV_1
	release
	end

Route102_EventScript_AmyRematch::
	trainerbattle_rematch_double TRAINER_AMY_AND_LIV_1, Route103_Text_AmyRematchIntro, Route103_Text_AmyRematchDefeated, Route103_Text_AmyRematchNotEnoughPokemon
	msgbox Route103_Text_AmyRematchPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Liv::
	trainerbattle_double TRAINER_AMY_AND_LIV_1, Route103_Text_LivIntro, Route103_Text_LivDefeated, Route103_Text_LivNotEnoughPokemon, Route102_EventScript_LivRegisterMatchCallAfterBattle
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route102_EventScript_LivRematch
	msgbox Route103_Text_LivPostBattle, MSGBOX_AUTOCLOSE
	end

Route102_EventScript_LivRegisterMatchCallAfterBattle::
	msgbox Route103_Text_AmyLivRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_AMY_AND_LIV_1
	release
	end

Route102_EventScript_LivRematch::
	trainerbattle_rematch_double TRAINER_AMY_AND_LIV_1, Route103_Text_LivRematchIntro, Route103_Text_LivRematchDefeated, Route103_Text_LivRematchNotEnoughPokemon
	msgbox Route103_Text_LivRematchPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Andrew::
	trainerbattle_single TRAINER_ANDREW, Route103_Text_AndrewIntro, Route103_Text_AndrewDefeated
	msgbox Route103_Text_AndrewPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Miguel::
	trainerbattle_single TRAINER_MIGUEL_1, Route103_Text_MiguelIntro, Route103_Text_MiguelDefeated, Route102_EventScript_MiguelRegisterMatchCallAfterBattle
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route103_EventScript_MiguelRematch
	msgbox Route103_Text_MiguelPostBattle, MSGBOX_DEFAULT
	release
	end

Route102_EventScript_MiguelRegisterMatchCallAfterBattle::
	special PlayerFaceTrainerAfterBattle
	waitmovement 0
	msgbox Route103_Text_MiguelRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_MIGUEL_1
	release
	end

Route103_EventScript_MiguelRematch::
	trainerbattle_rematch TRAINER_MIGUEL_1, Route103_Text_MiguelRematchIntro, Route103_Text_MiguelRematchDefeated
	msgbox Route103_Text_MiguelRematchPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Marcos::
	trainerbattle_single TRAINER_MARCOS, Route103_Text_MarcosIntro, Route103_Text_MarcosDefeated
	msgbox Route103_Text_MarcosPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Rhett::
	trainerbattle_single TRAINER_RHETT, Route103_Text_RhettIntro, Route103_Text_RhettDefeated
	msgbox Route103_Text_RhettPostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Pete::
	trainerbattle_single TRAINER_PETE, Route103_Text_PeteIntro, Route103_Text_PeteDefeated
	msgbox Route103_Text_PetePostBattle, MSGBOX_AUTOCLOSE
	end

Route103_EventScript_Isabelle::
	trainerbattle_single TRAINER_ISABELLE, Route103_Text_IsabelleIntro, Route103_Text_IsabelleDefeated
	msgbox Route103_Text_IsabellePostBattle, MSGBOX_AUTOCLOSE
	end

Route103_Text_MayRoute103Pokemon:
	.string "小遥：我看看……在103号道路\n发现的宝可梦有……$"

Route103_Text_MayLetsBattle:
	.string "啊，你好，{PLAYER}{KUN}！\p……哦，原来爸爸送你了\n一只宝可梦。\p机会难得，\n我们来一场对战吧！\p让我教你怎样当\n一位训练家！$"

Route103_Text_MayDefeated:
	.string "哇！好厉害！\n{PLAYER}{KUN}，你真棒！$"

Route103_Text_MayTimeToHeadBack:
	.string "小遥：现在我明白爸爸\n为什么会对你青睐有加了。\p你刚获得的宝可梦\n就已经这么亲近你了。\p好了，是我输了。\n我们差不多该回研究所……$"

Route103_Text_BrendanRoute103Pokemon:
	.string "小悠：好，原来103号道路上\n的宝可梦有这个和这个……$"

Route103_Text_BrendanLetsBattle:
	.string "嗨，{PLAYER}！\p……这样啊，\n爸爸给你了一只宝可梦吗？\p这样正好，\n我们来一场对战吧？\p我来教你如何\n成为一位训练家！$"

Route103_Text_BrendanDefeated:
	.string "呵，{PLAYER}，你还挺厉害的嘛。$"

Route103_Text_BrendanTimeToHeadBack:
	.string "小悠：我明白了……\n难怪爸爸会对你青睐有加。\p{PLAYER}，我想你说不定能和\n任何宝可梦都成为朋友呢！\p好了，既然是我输了，\n我们先回研究所……$"

Route103_Text_SeviperAmbush:
	.string "嘶……嘶嘶……！！\p{RIVAL}：那是……！？\n突然窜出来的……\p是野生的饭匙蛇！\n它盯上我们了！\p你在干什么？！快点迎击啊！\n面对这么凶暴的宝可梦，\p你不帮忙的话，待会连你也会遭殃的！\n我们一起上！$"

Route103_Text_RivalRetryAmbush:
	.string "{RIVAL}：你准备好了吗？\n我还撑的住！\p那条野生的饭匙蛇还在那里！\n这次我们一定要\p把它打倒！$"

Route103_Text_RivalPromise:
	.string "{RIVAL}：呼……总算打倒了。\p居然能在战斗里配合得这么默契，\n你呀，果然不简单。\p{PLAYER}，你的目标是什么？\n是收集徽章，还是制霸华丽大赛？\p我的目标，是帮助老爸收集图鉴，\n顺带成为世界最强的训练家！\p既然我们都刚出发，不如来比比看，\n看谁先在丰缘地区闯出名声！\p可别被我甩在后面了！\n那么，我们研究所见！$"

Route103_Text_ShouldHaveBroughtPotion:
	.string "我的宝可梦已经很累了……\n我本该买点伤药的……$"

Route103_Text_ShortcutToOldale:
	.string "如果从这儿渡海，\n就能到达古玫镇。\p呵呵呵，这条捷径会派上不少用场的。$"

Route103_Text_RouteSign:
	.string "103号道路\n{DOWN_ARROW}古玫镇$"
