RustboroCity_DevonCorp_3F_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, RustboroCity_DevonCorp_3F_OnTransition
	map_script MAP_SCRIPT_ON_WARP_INTO_MAP_TABLE, RustboroCity_DevonCorp_3F_OnWarp
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RustboroCity_DevonCorp_3F_OnFrame
	.byte 0

RustboroCity_DevonCorp_3F_OnTransition:
	call_if_eq VAR_DEVON_CORP_3F_STATE, 0, RustboroCity_DevonCorp_3F_EventScript_SetEmployeePos
	end

RustboroCity_DevonCorp_3F_EventScript_SetEmployeePos::
	setobjectxyperm LOCALID_DEVON_CORP_3F_EMPLOYEE, 3, 2
	setobjectmovementtype LOCALID_DEVON_CORP_3F_EMPLOYEE, MOVEMENT_TYPE_FACE_LEFT
	return

RustboroCity_DevonCorp_3F_OnWarp:
	map_script_2 VAR_DEVON_CORP_3F_STATE, 0, RustboroCity_DevonCorp_3F_EventScript_PlayerFaceEast
	.2byte 0

RustboroCity_DevonCorp_3F_EventScript_PlayerFaceEast::
	turnobject LOCALID_PLAYER, DIR_EAST
	end

RustboroCity_DevonCorp_3F_OnFrame:
	map_script_2 VAR_DEVON_CORP_3F_STATE, 0, RustboroCity_DevonCorp_3F_EventScript_MeetPresident
	.2byte 0

RustboroCity_DevonCorp_3F_EventScript_MeetPresident::
	lockall
	msgbox RustboroCity_DevonCorp_3F_Text_ThisIs3rdFloorWaitHere, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_DEVON_CORP_3F_EMPLOYEE, RustboroCity_DevonCorp_3F_Movement_EmployeeWalkOffscreen
	waitmovement 0
	delay 80
	applymovement LOCALID_DEVON_CORP_3F_EMPLOYEE, RustboroCity_DevonCorp_3F_Movement_EmployeeReturnToPlayer
	waitmovement 0
	msgbox RustboroCity_DevonCorp_3F_Text_WordWithPresidentComeWithMe, MSGBOX_DEFAULT
	closemessage
	playbgm MUS_FOLLOW_ME, FALSE
	applymovement LOCALID_DEVON_CORP_3F_EMPLOYEE, RustboroCity_DevonCorp_3F_Movement_LeadPlayerToPresident
	applymovement LOCALID_PLAYER, RustboroCity_DevonCorp_3F_Movement_PlayerFollowToPresident
	waitmovement 0
	msgbox RustboroCity_DevonCorp_3F_Text_PleaseGoAhead, MSGBOX_DEFAULT
	closemessage
	fadedefaultbgm
	applymovement LOCALID_DEVON_CORP_3F_EMPLOYEE, RustboroCity_DevonCorp_3F_Movement_EmployeeFaceDesk
	applymovement LOCALID_PLAYER, RustboroCity_DevonCorp_3F_Movement_PlayerApproachDesk
	waitmovement 0
	msgbox RustboroCity_DevonCorp_3F_Text_MrStoneNoticePokenav, MSGBOX_DEFAULT
	closemessage
	msgbox RustboroCity_DevonCorp_3F_Text_MrStoneIHaveFavor, MSGBOX_DEFAULT
	giveitem ITEM_LETTER
	msgbox RustboroCity_DevonCorp_3F_Text_MrStoneWantYouToHaveThis, MSGBOX_DEFAULT
	giveitem ITEM_EXP_CHARM
	setflag FLAG_HIDE_RUSTBORO_CITY_POKEMON_SCHOOL_SCOTT
	msgbox RustboroCity_DevonCorp_3F_Text_MrStoneExplainPokenavRestUp, MSGBOX_DEFAULT
	closemessage
	call Common_EventScript_OutOfCenterPartyHeal
	msgbox RustboroCity_DevonCorp_3F_Text_MrStoneGoWithCautionAndCare, MSGBOX_DEFAULT
	setflag FLAG_HIDE_ROUTE_116_WANDAS_BOYFRIEND
	clearflag FLAG_HIDE_RUSTURF_TUNNEL_WANDAS_BOYFRIEND
	clearflag FLAG_HIDE_RUSTURF_TUNNEL_WANDA
	clearflag FLAG_HIDE_BRINEYS_HOUSE_MR_BRINEY
	clearflag FLAG_HIDE_BRINEYS_HOUSE_PEEKO
	setvar VAR_BRINEY_LOCATION, 1
	clearflag FLAG_HIDE_RUSTBORO_CITY_RIVAL
	setvar VAR_DEVON_CORP_3F_STATE, 1
	setvar VAR_RUSTBORO_CITY_STATE, 6
	releaseall
	end

RustboroCity_DevonCorp_3F_Movement_Unused:
	walk_up
	walk_up
	walk_up
	walk_in_place_faster_left
	step_end

RustboroCity_DevonCorp_3F_Movement_LeadPlayerToPresident:
	walk_right
	walk_right
	walk_right
	walk_down
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_in_place_faster_left
	step_end

RustboroCity_DevonCorp_3F_Movement_EmployeeFaceDesk:
	delay_16
	walk_in_place_faster_down
	step_end

RustboroCity_DevonCorp_3F_Movement_EmployeeWalkOffscreen:
	walk_right
	walk_right
	walk_right
	walk_down
	walk_right
	walk_right
	walk_right
	walk_right
	step_end

RustboroCity_DevonCorp_3F_Movement_EmployeeReturnToPlayer:
	walk_left
	walk_left
	walk_left
	walk_left
	walk_up
	walk_left
	walk_left
	walk_left
	step_end

RustboroCity_DevonCorp_3F_Movement_PlayerFollowToPresident:
	walk_right
	walk_right
	walk_right
	walk_right
	walk_down
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	walk_right
	step_end

RustboroCity_DevonCorp_3F_Movement_PlayerApproachDesk:
	delay_16
	delay_16
	walk_down
	walk_down
	walk_in_place_faster_right
	step_end

RustboroCity_DevonCorp_3F_EventScript_MrStone::
	lock
	faceplayer
	goto_if_set FLAG_RECEIVED_EXP_SHARE, RustboroCity_DevonCorp_3F_EventScript_MrStoneAfterFavor
	goto_if_set FLAG_DELIVERED_STEVEN_LETTER, RustboroCity_DevonCorp_3F_EventScript_GiveExpShare
	msgbox RustboroCity_DevonCorp_3F_Text_CountingOnYou, MSGBOX_DEFAULT
	closemessage
	applymovement VAR_LAST_TALKED, Common_Movement_FaceOriginalDirection
	waitmovement 0
	release
	end

RustboroCity_DevonCorp_3F_EventScript_GiveExpShare::
	msgbox RustboroCity_DevonCorp_3F_Text_ThankYouForDeliveringLetter, MSGBOX_DEFAULT
	giveitem ITEM_EXP_SHARE
	goto_if_eq VAR_RESULT, FALSE, Common_EventScript_ShowBagIsFull
	setflag FLAG_RECEIVED_EXP_SHARE
	msgbox RustboroCity_DevonCorp_3F_Text_ExplainExpShare, MSGBOX_DEFAULT
	closemessage
	applymovement VAR_LAST_TALKED, Common_Movement_FaceOriginalDirection
	waitmovement 0
	release
	end

RustboroCity_DevonCorp_3F_EventScript_MrStoneAfterFavor::
	msgbox RustboroCity_DevonCorp_3F_Text_NotFamiliarWithTrends, MSGBOX_DEFAULT
	closemessage
	applymovement VAR_LAST_TALKED, Common_Movement_FaceOriginalDirection
	waitmovement 0
	release
	end

RustboroCity_DevonCorp_3F_EventScript_Employee::
	lock
	faceplayer
	goto_if_set FLAG_RECEIVED_REPEAT_BALL, RustboroCity_DevonCorp_3F_EventScript_EmployeeBalls
	msgbox RustboroCity_DevonCorp_3F_Text_VisitCaptSternShipyard, MSGBOX_DEFAULT
	release
	end

RustboroCity_DevonCorp_3F_EventScript_EmployeeBalls::
	msgbox RustboroCity_DevonCorp_3F_Text_RepeatAndTimerHugelyPopular, MSGBOX_DEFAULT
	release
	end

RustboroCity_DevonCorp_3F_EventScript_RareRocksDisplay::
	msgbox RustboroCity_DevonCorp_3F_Text_RareRocksDisplay, MSGBOX_SIGN
	end

RustboroCity_DevonCorp_3F_Text_MrStoneNoticePokenav:
    .string "社长：嗯？\p你手上那个……\n能让我看一下吗？\p……这宝可导航。\p奇怪，这编号\n是正式发售前的。\p你怎么会有？\p……原来如此。\n是千里寄给你的吗？\p这么说，你一定就是\n千里的孩子\l{PLAYER}{KUN}了。$"

RustboroCity_DevonCorp_3F_Text_MrStoneIHaveFavor:
    .string "我是兹伏奇，\n得文公司的社长。\p刚刚听说了你的事迹！\p你不止一次，而是两次\n救了我们公司的员工！\p我有件事想拜托给\n你这样出色的人。\p我知道你要送包裹到\n凯那市的船厂去。\p唔，在那之前，\n能不能顺路去武斗镇一趟？\p我希望你能把这封信交给\n武斗镇里一个叫大吾的人。$"

RustboroCity_DevonCorp_3F_Text_MrStoneWantYouToHaveThis:
    .string "兹伏奇：你知道，\n作为一位优秀的社长，\p我绝不会吝啬到\n让人白白帮忙。\p因此我把这个送给你！$"

RustboroCity_DevonCorp_3F_Text_MrStoneExplainPokenavRestUp:
    .string "兹伏奇：那是经验护符。\p只要你带在身上，\n宝可梦获得的经验值\l就会额外增加。\p另外，你的宝可导航里\n装了丰缘地区的地图，\p你可以很方便地查看\n武斗镇和凯那市的地点！\p对了，我听说一些邪恶的犯罪组织\n——我想是熔岩队和海洋队——\l在到处惹是生非，\p建议你出发前最好\n先好好休息一下。$"

RustboroCity_DevonCorp_3F_Text_MrStoneGoWithCautionAndCare:
    .string "兹伏奇：好了，{PLAYER}{KUN}，\n路上小心！$"

RustboroCity_DevonCorp_3F_Text_CountingOnYou:
    .string "兹伏奇：就拜托给你了！\n送到之后我有谢礼。$"

RustboroCity_DevonCorp_3F_Text_ThankYouForDeliveringLetter:
    .string "兹伏奇：你把信送到了？\n非常感谢！\p这是我的谢礼，\n应该对你这样的训练家有所帮助。$"

RustboroCity_DevonCorp_3F_Text_ExplainExpShare:
    .string "兹伏奇：携带这个学习装置的宝可梦，\n即使没有实际参与战斗，\l也能分享到部分经验值。\p对于培养弱小的宝可梦来说，\n学习装置非常实用。$"

RustboroCity_DevonCorp_3F_Text_NotFamiliarWithTrends:
    .string "兹伏奇：我从年轻时起，\n就一直全身心投入工作，\p所以我也不怎么了解\n流行趋势之类的东西。\p现在的年轻人们都想成为\n宝可梦联盟的训练家吗？$"

RustboroCity_DevonCorp_3F_Text_ThisIs3rdFloorWaitHere:
    .string "这里是得文公司的3楼。\p社长办公室的\n所在地。\p无论如何，我无法表明\n我对你所做的事情的感谢。\p呃……另外，你帮我们\n取回来的那个包裹……\p能再请你帮忙\n送到凯那市的船厂吗？\p如果再让那些强盗\n抢走就糟了。\p呃……哈哈哈，那，呃，\n能帮我这个忙吗？\p啊，对了，\n请在此稍等一下。$"

RustboroCity_DevonCorp_3F_Text_WordWithPresidentComeWithMe:
    .string "我们的社长有话\n想跟你说。\p跟我来。$"

RustboroCity_DevonCorp_3F_Text_PleaseGoAhead:
    .string "过来吧。$"

RustboroCity_DevonCorp_3F_Text_VisitCaptSternShipyard:
    .string "你到凯那市的船厂后，\n应该去找楠木馆长。$"

RustboroCity_DevonCorp_3F_Text_RepeatAndTimerHugelyPopular:
    .string "得文的新产品重复球\n和时间球很快就\l在训练家之中流行起来了。$"

RustboroCity_DevonCorp_3F_Text_RareRocksDisplay:
    .string "这是社长收集的\n稀有岩石。$"

