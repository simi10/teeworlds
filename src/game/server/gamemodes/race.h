/* copyright (c) 2007 rajh and gregwar. Score stuff */
#ifndef GAME_SERVER_GAMEMODES_RACE_H
#define GAME_SERVER_GAMEMODES_RACE_H

#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>

class CGameControllerRACE : public IGameController
{
	
#if defined(CONF_TEERACE)
	// demo
	int m_aStopRecordTick[MAX_CLIENTS];
#endif

public:
	CGameControllerRACE(class CGameContext *pGameServer);
	~CGameControllerRACE();

	// race
	enum
	{
		RACE_NONE = 0,
		RACE_STARTED,
		RACE_FINISHED,
	};

	// race
	struct CRaceData
	{
		int m_RaceState;
		int m_StartTime;
		int m_RefreshTime;

		float m_aCpCurrent[25];
		int m_CpTick;
		float m_CpDiff;
		
		float m_StartAddTime;

		void Reset()
		{
			m_RaceState = RACE_NONE;
			m_StartTime = -1;
			m_RefreshTime = -1;
			mem_zero(m_aCpCurrent, sizeof(m_aCpCurrent));
			m_CpTick = -1;
			m_CpDiff = 0;
			m_StartAddTime = 0.0f;
		}
	} m_aRace[MAX_CLIENTS];

	vec2 *m_pTeleporter;

	// game
	virtual void DoWincheckMatch();

	// event
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);

	virtual bool OnCheckpoint(int ID, int z);
	virtual bool OnRaceStart(int ID, float StartAddTime, bool Check=true);
	virtual bool OnRaceEnd(int ID, float FinishTime);

	// general
	virtual void Tick();

	void InitTeleporter();
	float GetTime(int ID);
};

#endif
