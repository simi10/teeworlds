#ifndef GAME_SERVER_GAMEMODES_CTF_H
#define GAME_SERVER_GAMEMODES_CTF_H
#include "race.h"

class CGameControllerFC : public CGameControllerRACE
{
	// balancing
	virtual bool CanBeMovedOnBalance(int Cid);

	// game
	class CFlag *m_apFlags[2];
	class CFlag *m_apPlFlags[MAX_CLIENTS];

public:
	CGameControllerFC(class CGameContext *pGameServer);
	
	// info
	virtual bool IsFastCap() { return true; }

	bool IsOwnFlagStand(vec2 Pos, int Team);
	bool IsEnemyFlagStand(vec2 Pos, int Team);
	
	// event
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	virtual bool OnRaceStart(int ID, float StartAddTime, bool Check);
	virtual bool OnRaceEnd(int ID, float FinishTime);

	// spawn
	virtual bool CanSpawn(int Team, vec2 *pOutPos);

	// general
	virtual void Snap(int SnappingClient);
};

#endif
