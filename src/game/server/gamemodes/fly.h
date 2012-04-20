#ifndef GAME_SERVER_GAMEMODES_FLY_H
#define GAME_SERVER_GAMEMODES_FLY_H
#include <game/server/gamecontroller.h>

class CGameControllerFLY : public IGameController
{
	// balancing
	virtual bool CanBeMovedOnBalance(int ClientID);

	// game
	vec2 *m_pTeleporter;
	class CFlag *m_apFlags[2];

	virtual void DoWincheckMatch();

	// general
	void InitTeleporter();

public:
	CGameControllerFLY(class CGameContext *pGameServer);
	
	// event
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual bool OnEntity(int Index, vec2 Pos);

	// general
	virtual void Snap(int SnappingClient);
	virtual void Tick();

	vec2 GetTeleporter(int ID) { return m_pTeleporter[ID]; }
};

#endif
