/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_CTF_H
#define GAME_SERVER_GAMEMODES_CTF_H
/*#include <game/server/gamecontroller.h>
#include <game/server/entity.h>

class CGameControllerCTF : public IGameController
{
	// balancing
	virtual bool CanBeMovedOnBalance(int ClientID);

	// game
	class CFlag *m_apFlags[2];

	virtual void DoWincheckMatch();

public:
	CGameControllerCTF(class CGameContext *pGameServer);
	
	// event
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual bool OnEntity(int Index, vec2 Pos);

	// general
	virtual void Snap(int SnappingClient);
	virtual void Tick();
};

// TODO: move to seperate file
class CFlag : public CEntity
{
public:
	static const int ms_PhysSize = 14;
	CCharacter *m_pCarryingCharacter;
	vec2 m_Vel;
	vec2 m_StandPos;
	
	int m_Team;
	int m_AtStand;
	int m_DropTick;
	int m_GrabTick;
	
	CFlag(CGameWorld *pGameWorld, int Team);

	virtual void Reset();
	virtual void Snap(int SnappingClient);
};*/
#endif

