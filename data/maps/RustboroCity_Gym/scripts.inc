RustboroCity_Gym_MapScripts::
	.byte 0

RustboroCity_Gym_EventScript_Roxanne::
	trainerbattle_single TRAINER_ROXANNE_1, RustboroCity_Gym_Text_RoxanneIntro, RustboroCity_Gym_Text_RoxanneDefeat, RustboroCity_Gym_EventScript_RoxanneDefeated, NO_MUSIC
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, RustboroCity_Gym_EventScript_RoxanneRematch
	goto_if_unset FLAG_RECEIVED_TM_ROCK_TOMB, RustboroCity_Gym_EventScript_GiveRockTomb
	msgbox RustboroCity_Gym_Text_RoxannePostBattle, MSGBOX_DEFAULT
	release
	end

RustboroCity_Gym_EventScript_RoxanneDefeated::
	message RustboroCity_Gym_Text_ReceivedStoneBadge
	waitmessage
	call Common_EventScript_PlayGymBadgeFanfare
	msgbox RustboroCity_Gym_Text_StoneBadgeInfoTakeThis, MSGBOX_DEFAULT
	setflag FLAG_DEFEATED_RUSTBORO_GYM
	setflag FLAG_BADGE01_GET
	setvar VAR_RUSTBORO_CITY_STATE, 1
	addvar VAR_PETALBURG_GYM_STATE, 1
	setvar VAR_0x8008, 1
	call Common_EventScript_SetGymTrainers
	call_if_eq VAR_PETALBURG_GYM_STATE, 6, Common_EventScript_ReadyPetalburgGymForBattle
	goto RustboroCity_Gym_EventScript_GiveRockTomb
	end

RustboroCity_Gym_EventScript_GiveRockTomb::
	giveitem ITEM_TM_ROCK_TOMB
	goto_if_eq VAR_RESULT, FALSE, Common_EventScript_ShowBagIsFull
	setflag FLAG_RECEIVED_TM_ROCK_TOMB
	msgbox RustboroCity_Gym_Text_ExplainRockTomb, MSGBOX_DEFAULT
	release
	end

RustboroCity_Gym_EventScript_RoxanneRematch::
	trainerbattle_rematch_double TRAINER_ROXANNE_1, RustboroCity_Gym_Text_RoxannePreRematch, RustboroCity_Gym_Text_RoxanneRematchDefeat, RustboroCity_Gym_Text_RoxanneRematchNeedTwoMons
	msgbox RustboroCity_Gym_Text_RoxannePostRematch, MSGBOX_AUTOCLOSE
	end

RustboroCity_Gym_EventScript_Josh::
	trainerbattle_single TRAINER_JOSH, RustboroCity_Gym_Text_JoshIntro, RustboroCity_Gym_Text_JoshDefeat
	msgbox RustboroCity_Gym_Text_JoshPostBattle, MSGBOX_AUTOCLOSE
	end

RustboroCity_Gym_EventScript_Tommy::
	trainerbattle_single TRAINER_TOMMY, RustboroCity_Gym_Text_TommyIntro, RustboroCity_Gym_Text_TommyDefeat
	msgbox RustboroCity_Gym_Text_TommyPostBattle, MSGBOX_AUTOCLOSE
	end

RustboroCity_Gym_EventScript_Marc::
	trainerbattle_single TRAINER_MARC, RustboroCity_Gym_Text_MarcIntro, RustboroCity_Gym_Text_MarcDefeat
	msgbox RustboroCity_Gym_Text_MarcPostBattle, MSGBOX_AUTOCLOSE
	end

RustboroCity_Gym_EventScript_GymGuide::
	lock
	faceplayer
	goto_if_set FLAG_DEFEATED_RUSTBORO_GYM, RustboroCity_Gym_EventScript_GymGuidePostVictory
	msgbox RustboroCity_Gym_Text_GymGuideAdvice, MSGBOX_DEFAULT
	release
	end

RustboroCity_Gym_EventScript_GymGuidePostVictory::
	msgbox RustboroCity_Gym_Text_GymGuidePostVictory, MSGBOX_DEFAULT
	release
	end

RustboroCity_Gym_EventScript_LeftGymStatue::
	lockall
	goto_if_set FLAG_BADGE01_GET, RustboroCity_Gym_EventScript_GymStatueCertified
	goto RustboroCity_Gym_EventScript_GymStatue
	end

RustboroCity_Gym_EventScript_RightGymStatue::
	lockall
	goto_if_set FLAG_BADGE01_GET, RustboroCity_Gym_EventScript_GymStatueCertified
	goto RustboroCity_Gym_EventScript_GymStatue
	end

RustboroCity_Gym_EventScript_GymStatueCertified::
	msgbox RustboroCity_Gym_Text_GymStatueCertified, MSGBOX_DEFAULT
	releaseall
	end

RustboroCity_Gym_EventScript_GymStatue::
	msgbox RustboroCity_Gym_Text_GymStatue, MSGBOX_DEFAULT
	releaseall
	end

RustboroCity_Gym_EventScript_RegisterRoxanne::
	lockall
	pokenavcall RustboroCity_Gym_Text_RoxanneRegisterCall
	waitmessage
	delay 30
	playfanfare MUS_REGISTER_MATCH_CALL
	msgbox RustboroCity_Gym_Text_RegisteredRoxanne, MSGBOX_DEFAULT
	waitfanfare
	closemessage
	delay 30
	setflag FLAG_ENABLE_ROXANNE_MATCH_CALL
	clearflag FLAG_ENABLE_ROXANNE_FIRST_CALL
	releaseall
	end

RustboroCity_Gym_Text_GymGuideAdvice:
    .string "哟，怎么样？\p听好，朋友！\n你想成为冠军吗？\p我不是训练家，但我可以\n给你一些取胜的建议。\p那么说定了！我们一起朝着\n宝可梦冠军的目标前进！\p作为训练家，任务之一\n就是收集道馆徽章，对吗？\p但击败道馆馆主并没那么容易！\p因此我才会在这里！\n我会给你专业的建议！\p道馆馆主杜娟\n使用岩石属性宝可梦。\p岩石属性非常坚固，\n但它抵挡不了水属性和\l草属性招式。\p你打败道馆馆主之后\n再来找我吧。\p好了，加油！$"

RustboroCity_Gym_Text_GymGuidePostVictory:
    .string "哇！令人心驰的胜利！\n看来我的鼓励派上了用场！\p太好了！\n看看你的训练家卡，\p你得到的岩石徽章\n就记录在上面。\p换句话说……\p你踏上了通向冠军之路的\n第一级阶梯！\p感觉不错！$"

RustboroCity_Gym_Text_JoshIntro:
    .string "别小看我们道馆训练家！\n我会告诉你为什么我是最好的！$"

RustboroCity_Gym_Text_JoshDefeat:
    .string "你比我厉害多了……$"

RustboroCity_Gym_Text_JoshPostBattle:
    .string "你还没有体会过岩石属性的\n可怕力量！$"

RustboroCity_Gym_Text_TommyIntro:
    .string "如果你不能打败我，\n就不可能打败杜娟！$"

RustboroCity_Gym_Text_TommyDefeat:
    .string "哇！你的潜力很大！$"

RustboroCity_Gym_Text_TommyPostBattle:
    .string "杜娟可比我厉害好几倍。\p你最好提高警惕！$"

RustboroCity_Gym_Text_MarcIntro:
    .string "虽然这里是城镇中心，\n但那又如何？\p我的岩石宝可梦可需要\n大闹一场的空间！$"

RustboroCity_Gym_Text_MarcDefeat:
    .string "哦，天哪！\n我们的挑战者这么强大！$"

RustboroCity_Gym_Text_MarcPostBattle:
    .string "只有交给我们的馆主了。\p这么年轻就培育岩石属性，\n真是辛苦她了。$"

RustboroCity_Gym_Text_RoxanneIntro:
    .string "您好，我是卡那兹市宝可梦道馆的\n馆主杜娟。\p我想要把在宝可梦训练家学校中\n所学到的东西都运用到宝可梦对战\l里面，所以才成为了道馆馆主。\p您要使用哪种宝可梦，\n如何对战，可以告诉我吗？$"

RustboroCity_Gym_Text_RoxanneDefeat:
    .string "啊……\n是我输了……\p看来我还有\n许多东西要学……\p我知道了。\p宝可梦联盟规定\n战胜道馆馆主的训练家\l可以得到这个。\p请收下这枚宝可梦联盟\n公认的岩石徽章。$"

RustboroCity_Gym_Text_ReceivedStoneBadge:
    .string "{PLAYER}从杜娟那里\n得到了岩石徽章。$"

RustboroCity_Gym_Text_StoneBadgeInfoTakeThis:
    .string "{CREATE_MUGSHOT MUGSHOT_TEST EMOTE_DUJUA}"
    .string "岩石徽章能提高您的\n宝可梦的攻击力。\p有了它，您的宝可梦也能在对战外\n使用秘传招式居合劈了。\p然后……\n请您也收下这个。$"

RustboroCity_Gym_Text_ExplainRockTomb:
    .string "{CREATE_MUGSHOT MUGSHOT_TEST EMOTE_DUJUA}"
    .string "那个招式学习器39中\n装着岩石封锁。\p用落石攻击对手的同时\n也能降低对手的速度。\p如果您使用招式学习器，\n宝可梦能立刻学会其中的招式。\p但一个招式学习器使用后就会消失，\n所以使用前要仔细考虑才行呢。$"

RustboroCity_Gym_Text_RoxannePostBattle:
    .string "{CREATE_MUGSHOT MUGSHOT_TEST EMOTE_DUJUA}"
    .string "像您这么强的训练家为什么不去\n其他道馆挑战看看呢？\p通过和许许多多的训练家对战，\n您能够学到很多知识哟。$"

RustboroCity_Gym_Text_GymStatue:
    .string "卡那兹市宝可梦道馆$"

RustboroCity_Gym_Text_GymStatueCertified:
    .string "卡那兹市宝可梦道馆\p杜娟认可的训练家：\n{PLAYER}$"

RustboroCity_Gym_Text_RoxanneRegisterCall:
    .string "…… …… ……\n…… …… ……哔！\p杜娟：哦，您好，是{PLAYER}吗？\n我是卡那兹市的杜娟。\p我从藤树那儿听说您已经\n是个小有名气的训练家了。\p但是，我认为我不会\n再输给您了。\p我保证，我会提升道馆的实力，\n您可以再来挑战我们。\p我希望您会像我一样期待\n那个时刻的到来！\p…… …… ……\n…… ……咔哒！$"

RustboroCity_Gym_Text_RegisteredRoxanne:
    .string "把道馆馆主杜娟\n登记到宝可导航里了。$"

RustboroCity_Gym_Text_RoxannePreRematch:
    .string "杜娟：我真高兴能再见到您。\n我是这个道馆的馆主杜娟。\p我相信我们可以再享受很多场\n像上次那样精彩的对战。\p我想看看我们彼此都变强了多少。\n让我们开始吧！$"

RustboroCity_Gym_Text_RoxanneRematchDefeat:
    .string "呃……\n我又输了……$"

RustboroCity_Gym_Text_RoxannePostRematch:
    .string "{CREATE_MUGSHOT MUGSHOT_TEST EMOTE_DUJUA}"
    .string "杜娟：关于宝可梦对战，\n我还有许多事要学习。\p察觉到了这一点，\n让我更喜欢宝可梦对战了！$"

RustboroCity_Gym_Text_RoxanneRematchNeedTwoMons:
    .string "{CREATE_MUGSHOT MUGSHOT_TEST EMOTE_DUJUA}"
    .string "杜娟：我真高兴能再见到您。\n我是这个道馆的馆主杜娟。\p我相信我们可以再享受很多场\n像上次那样精彩的对战。\p我想看看我们彼此都变强了……\p哎，不……\n您只带了1只宝可梦？\p带至少2只\n宝可梦回来吧。$"
