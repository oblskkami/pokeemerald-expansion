LALUS_MapScripts::
    map_script MAP_SCRIPT_ON_TRANSITION, LALUS_OnTransition
	.byte 0

LALUS_OnTransition:
	setflag FLAG_VISITED_LALUS
    setvar VAR_CONTEST_HALL_STATE, 0
    end

