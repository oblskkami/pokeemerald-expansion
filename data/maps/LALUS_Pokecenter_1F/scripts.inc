LALUS_Pokecenter_1F_MapScripts::
    map_script MAP_SCRIPT_ON_TRANSITION, LALUS_PokeCenter_1F_OnTransition
	map_script MAP_SCRIPT_ON_RESUME, CableClub_OnResume
	.byte 0

LALUS_PokeCenter_1F_OnTransition:
	setrespawn HEAL_LOCATION_LALUS
	call Common_EventScript_UpdateBrineyLocation
	end

LALUS_Pokecenter_1F_EventScript_Nurse::
	setvar VAR_0x800B, LOCALID_LALUS_NURSE
	call Common_EventScript_PkmnCenterNurse
	waitmessage
	waitbuttonpress
	release
	end

