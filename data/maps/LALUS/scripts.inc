LALUS_MapScripts::
    map_script MAP_SCRIPT_ON_TRANSITION, LALUS_OnTransition
	.byte 0

LALUS_OnTransition:
	setflag FLAG_VISITED_LALUS
    setvar VAR_CONTEST_HALL_STATE, 0
    end

LALUS_EventScript_Steven::
    lock
	faceplayer
    msgbox LALUS_Steven_songdongxiText, MSGBOX_DEFAULT
    giveitem ITEM_TERA_ORB
    setflag FLAG_RECEIVED_TERA_ORB
    setflag FLAG_HIDE_LALUS_STEVEN_OUTSIDE
    msgbox LALUS_Steven_jieshiText,MSGBOX_DEFAULT 
    closemessage
    setflag FLAG_TERA_ORB_CR
    setfieldeffectargument 0, 1
    dofieldeffect FLDEFF_NPCFLY_OUT
    delay 15
    removeobject LOCALID_LALUS_STEVEN
    waitfieldeffect FLDEFF_NPCFLY_OUT
    release
	end

LALUS_Steven_songdongxiText:
    .string "大吾:嗨, {PLAYER}! 真巧!\n没想到在这能遇到你。\p"
    .string "正好,不用去额外找你了。\n"
    .string "请收下这个,这是我的一点心意。$"

LALUS_Steven_jieshiText:
    .string "大吾:太晶珠是我在帕底亚地区\n得到的。\p"
    .string "通过消耗能量,\n它能让精灵掌握一种\p名为太晶化的力量。\n"
    .string "能量只够在每场的战斗中使用一次。\p"
    .string "太晶珠失去能量的话,\n就去精灵中心充能吧。$"

