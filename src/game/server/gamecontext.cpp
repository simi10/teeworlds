/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <string.h>
#include <stdio.h>
#include <base/math.h>
#include <base/tl/sorted_array.h>
#include <engine/shared/packer.h>
#include <engine/shared/config.h>
#include <engine/shared/memheap.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/storage.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/version.h>

#include "entities/character.h"

/* no need for this
#include "gamemodes/dm.h"
#include "gamemodes/ctf.h"
#include "gamemodes/lms.h"
#include "gamemodes/mod.h"
#include "gamemodes/sur.h"
#include "gamemodes/tdm.h"*/
#include "gamemodes/race.h"
#include "gamemodes/fastcap.h"

#include "gamecontext.h"
#include "player.h"

#include "score.h"
#include "score/file_score.h"
#if defined(CONF_SQL)
#include "score/sql_score.h"
#endif
#if defined(CONF_TEERACE)
#include "score/wa_score.h"
#include <engine/external/json/writer.h>
#include "webapp.h"
#endif

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
	
	m_pScore = 0;
#if defined(CONF_TEERACE)
	m_pWebapp = 0;
	m_LastPing = -1;
#endif
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	delete m_pChatConsole;
	m_pChatConsole = 0;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;
#if defined(CONF_TEERACE)
	CServerWebapp *pWebapp = m_pWebapp;
#endif

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
#if defined(CONF_TEERACE)
	m_pWebapp = pWebapp;
#endif
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount, int Owner)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd), CmaskRace(this, Owner));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion), CmaskRace(this, Owner));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l;
			if((int)Dmg)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn), CmaskRace(this, ClientID));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death), CmaskRace(this, ClientID));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}


void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}


void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendRecord(int ClientID)
{
	CNetMsg_Sv_Record Msg;
	Msg.m_Time = (int)((Score()->GetRecord()->m_Time * 10000.0f + 9.0f) / 10.0f); // TODO: on next major release simply cast

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->m_IsUsingRaceClient)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	else
	{
		if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsUsingRaceClient)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

// 
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && (!str_comp_num(m_aVoteCommand, "kick ", 5) ||
		!str_comp_num(m_aVoteCommand, "set_team ", 9) || (!str_comp_num(m_aVoteCommand, "ban ", 4) && Server()->IsBanned(ClientID))))
		m_VoteCloseTime = -1;
}

void CGameContext::AbortVoteOnTeamChange(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && !str_comp_num(m_aVoteCommand, "set_team ", 9))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->GetGameType(), "DM")==0 ||
		str_comp(m_pController->GetGameType(), "TDM")==0 ||
		str_comp(m_pController->GetGameType(), "CTF")==0 ||
		str_comp(m_pController->GetGameType(), "LMS")==0 ||
		str_comp(m_pController->GetGameType(), "SUR")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;
	
	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_pController->DoTeamChange(m_apPlayers[i], m_apPlayers[i]->GetTeam()^1, false);
	}
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();
	
#if defined(CONF_TEERACE)
	if(m_pWebapp)
	{
		m_pWebapp->Update();
		m_pWebapp->Tick();
		if(m_LastPing == -1 || m_LastPing+Server()->TickSpeed()*60 < Server()->Tick())
		{
			Json::Value Data;
			Json::FastWriter Writer;
			
			int Num = 0;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_apPlayers[i])
				{
					if(Server()->GetUserID(i) > 0)
					{
						char aBuf[16];
						str_format(aBuf, sizeof(aBuf), "%d", Server()->GetUserID(i));
						char aName[MAX_NAME_LENGTH];
						str_copy(aName, Server()->ClientName(i), sizeof(aName));
						str_sanitize_strong(aName);
						Data["users"][aBuf] = aName;
					}
					else
					{
						char aName[MAX_NAME_LENGTH];
						str_copy(aName, Server()->ClientName(i), sizeof(aName));
						str_sanitize_strong(aName);
						Data["anonymous"][Num] = aName;
						Num++;
					}
				}
			}
			
			Data["map"] = g_Config.m_SvMap;

			std::string Json = Writer.write(Data);
			
			CRequest *pRequest = m_pWebapp->CreateRequest("ping/", CRequest::HTTP_POST);
			pRequest->SetBody(Json.c_str(), Json.length());
			m_pWebapp->SendRequest(pRequest, WEB_PING_PING);
			
			m_LastPing = Server()->Tick();
		}
	}
#endif

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}


#ifdef CONF_DEBUG
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->IsDummy())
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[i]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
#if defined(CONF_TEERACE)
void CGameContext::OnTeeraceAuth(int ClientID, const char *pStr, int SendRconCmds)
{
	if(str_comp_num(pStr, "teerace:", 8) == 0)
	{
		char m_aToken[32];
		if(m_pWebapp && Server()->GetUserID(ClientID) <= 0 && sscanf(pStr, "teerace:%s", m_aToken) == 1)
		{
			Json::Value Data;
			Json::FastWriter Writer;
			Data["api_token"] = m_aToken;
			std::string Json = Writer.write(Data);

			CWebUserAuthData *pUserData = new CWebUserAuthData();
			pUserData->m_ClientID = ClientID;
			pUserData->m_SendRconCmds = SendRconCmds;

			CRequest *pRequest = m_pWebapp->CreateRequest("users/auth_token/", CRequest::HTTP_POST);
			pRequest->SetBody(Json.c_str(), Json.length());
			m_pWebapp->SendRequest(pRequest, WEB_USER_AUTH, pUserData);
		}
	}
}
#endif

void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	int NumCorrections = m_NetObjHandler.NumObjCorrections();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == 0)
	{
		if(g_Config.m_Debug && NumCorrections != m_NetObjHandler.NumObjCorrections())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.CorrectedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
	}
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
	{
		int NumCorrections = m_NetObjHandler.NumObjCorrections();
		if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == 0)
		{
			if(g_Config.m_Debug && NumCorrections != m_NetObjHandler.NumObjCorrections())
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.CorrectedObjOn());
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
			m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
		}
	}
}

void CGameContext::OnClientEnter(int ClientID)
{
	m_pController->OnPlayerConnect(m_apPlayers[ClientID]);

	m_apPlayers[ClientID]->m_Score = -9999;
	
	// init the player
	Score()->PlayerData(ClientID)->Reset();
	Score()->LoadScore(ClientID);

	m_VoteUpdate = true;

	// update client infos (others before local)
	CNetMsg_Sv_ClientInfo NewClientInfoMsg;
	NewClientInfoMsg.m_ClientID = ClientID;
	NewClientInfoMsg.m_Local = 0;
	NewClientInfoMsg.m_Team = m_apPlayers[ClientID]->GetTeam();
	NewClientInfoMsg.m_pName = Server()->ClientName(ClientID);
	NewClientInfoMsg.m_pClan = Server()->ClientClan(ClientID);
	NewClientInfoMsg.m_Country = Server()->ClientCountry(ClientID);
	for(int p = 0; p < 6; p++)
	{
		NewClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p];
		NewClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p];
		NewClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p];
	}
	

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ClientID || !m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;

		// new info for others
		Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);

		// existing infos for new player
		CNetMsg_Sv_ClientInfo ClientInfoMsg;
		ClientInfoMsg.m_ClientID = i;
		ClientInfoMsg.m_Local = 0;
		ClientInfoMsg.m_Team = m_apPlayers[i]->GetTeam();
		ClientInfoMsg.m_pName = Server()->ClientName(i);
		ClientInfoMsg.m_pClan = Server()->ClientClan(i);
		ClientInfoMsg.m_Country = Server()->ClientCountry(i);
		for(int p = 0; p < 6; p++)
		{
			ClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[i]->m_TeeInfos.m_aaSkinPartNames[p];
			ClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[i]->m_TeeInfos.m_aUseCustomColors[p];
			ClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[i]->m_TeeInfos.m_aSkinPartColors[p];
		}
		Server()->SendPackMsg(&ClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);
	}

	// local info
	NewClientInfoMsg.m_Local = 1;
	Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);	

#if defined(CONF_TEERACE)
	if(m_pWebapp && Server()->GetUserID(ClientID) > 0)
	{
		Json::Value Userdata;
		Json::FastWriter Writer;
		Userdata["skin_name"] = pMsg->m_pSkin;
		if(pMsg->m_UseCustomColor)
		{
			Userdata["body_color"] = HslToRgb(pMsg->m_ColorBody);
			Userdata["feet_color"] = HslToRgb(pMsg->m_ColorFeet);
		}
		std::string Json = Writer.write(Userdata);

		char aURI[128];
		str_format(aURI, sizeof(aURI), "users/skin/%d/", Server()->GetUserID(ClientID));
		CRequest *pRequest = m_pWebapp->CreateRequest(aURI, CRequest::HTTP_PUT);
		pRequest->SetBody(Json.c_str(), Json.length());
		m_pWebapp->SendRequest(pRequest, WEB_USER_UPDATESKIN);
	}
#endif
}

void CGameContext::OnClientConnected(int ClientID, bool Dummy)
{
	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, Dummy);
	
	if(Dummy)
		return;

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnClientTeamChange(int ClientID)
{
	if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		AbortVoteOnTeamChange(ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
#if defined(CONF_TEERACE)
	// add job to update play time
	int UserID = Server()->GetUserID(ClientID);
	if(m_pWebapp && UserID > 0)
	{
		// calculate time in seconds
		Json::Value Post;
		Json::FastWriter Writer;
		Post["seconds"] = Server()->GetPlayTicks(ClientID)/Server()->TickSpeed();
		std::string Json = Writer.write(Post);

		char aURI[128];
		str_format(aURI, sizeof(aURI), "users/playtime/%d/", UserID);
		CRequest *pRequest = m_pWebapp->CreateRequest(aURI, CRequest::HTTP_PUT);
		pRequest->SetBody(Json.c_str(), Json.length());
		m_pWebapp->SendRequest(pRequest, WEB_USER_PLAYTIME);
	}
#endif

	AbortVoteOnDisconnect(ClientID);
	m_pController->OnPlayerDisconnect(m_apPlayers[ClientID]);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	// update clients on drop
	CNetMsg_Sv_ClientDrop Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_pReason = pReason;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	m_VoteUpdate = true;
}

#if defined(CONF_TEERACE)
// TODO: rework this / move this somewhere else
float HueToRgb(float v1, float v2, float h)
{
   if(h < 0.0f) h += 1;
   if(h > 1.0f) h -= 1;
   if((6.0f * h) < 1.0f) return v1 + (v2 - v1) * 6.0f * h;
   if((2.0f * h) < 1.0f) return v2;
   if((3.0f * h) < 2.0f) return v1 + (v2 - v1) * ((2.0f/3.0f) - h) * 6.0f;
   return v1;
}

int HslToRgb(int v)
{
	vec3 HSL = vec3(((v>>16)&0xff)/255.0f, ((v>>8)&0xff)/255.0f, 0.5f+(v&0xff)/255.0f*0.5f);
	vec3 RGB;
	if(HSL.s == 0.0f)
		RGB = vec3(HSL.l, HSL.l, HSL.l);
	else
	{
		float v2 = HSL.l < 0.5f ? HSL.l * (1.0f + HSL.s) : (HSL.l+HSL.s) - (HSL.s*HSL.l);
		float v1 = 2.0f * HSL.l - v2;

		RGB = vec3(HueToRgb(v1, v2, HSL.h + (1.0f/3.0f)), HueToRgb(v1, v2, HSL.h), HueToRgb(v1, v2, HSL.h - (1.0f/3.0f)));
	}
	
	RGB = RGB*255;
	return (((int)RGB.r)<<16)|(((int)RGB.g)<<8)|(int)RGB.b;
}
#endif

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg && g_Config.m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		return;
	}

	if(MsgID == NETMSGTYPE_CL_SAY)
	{
		CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
		int Team = pMsg->m_Team;
		if(Team)
			Team = pPlayer->GetTeam();
		else
			Team = CGameContext::CHAT_ALL;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
			pPlayer->m_LastChat = Server()->Tick();
		else
 		{
			pPlayer->m_LastChat = Server()->Tick();

			if(pMsg->m_pMessage[0] == '/')
			{
				m_ChatConsoleClientID = ClientID;
				ChatConsole()->ExecuteLine(pMsg->m_pMessage + 1);
				m_ChatConsoleClientID = -1;
			}
			else
			{
				// check for invalid chars
				unsigned char *pMessage = (unsigned char *)pMsg->m_pMessage;
				while (*pMessage)
				{
					if(*pMessage < 32)
						*pMessage = ' ';
					pMessage++;
				}

				SendChat(ClientID, Team, pMsg->m_pMessage);
			}
		}
	}
	else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
	{
		if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		int64 Now = Server()->Tick();
		pPlayer->m_LastVoteTry = Now;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		{
			SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			return;
		}

		if(m_VoteCloseTime)
		{
			SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
			return;
		}

		int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
		if(pPlayer->m_LastVoteCall && Timeleft > 0)
		{
			char aChatmsg[512] = {0};
			str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote", (Timeleft/Server()->TickSpeed())+1);
			SendChatTarget(ClientID, aChatmsg);
			return;
		}

		char aChatmsg[512] = {0};
		char aDesc[VOTE_DESC_LENGTH] = {0};
		char aCmd[VOTE_CMD_LENGTH] = {0};
		CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
		const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

		if(str_comp_nocase(pMsg->m_Type, "option") == 0)
		{
			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption)
			{
				if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
								pOption->m_aDescription, pReason);
					str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
					str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
					break;
				}

				pOption = pOption->m_pNext;
			}

			if(!pOption)
			{
				str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}
		}
		else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
		{
			if(!g_Config.m_SvVoteKick)
			{
				SendChatTarget(ClientID, "Server does not allow voting to kick players");
				return;
			}

			if(g_Config.m_SvVoteKickMin)
			{
				int PlayerNum = 0;
				for(int i = 0; i < MAX_CLIENTS; ++i)
					if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						++PlayerNum;

				if(PlayerNum < g_Config.m_SvVoteKickMin)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players on the server", g_Config.m_SvVoteKickMin);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
			}

			int KickID = str_toint(pMsg->m_Value);
			if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
			{
				SendChatTarget(ClientID, "Invalid client id to kick");
				return;
			}
			if(KickID == ClientID)
			{
				SendChatTarget(ClientID, "You can't kick yourself");
				return;
			}
			if(Server()->IsAuthed(KickID))
			{
				SendChatTarget(ClientID, "You can't kick admins");
				char aBufKick[128];
				str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
				SendChatTarget(KickID, aBufKick);
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
			str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
			if (!g_Config.m_SvVoteKickBantime)
				str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
			else
			{
				char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
				Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
				str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
			}
			m_VoteClientID = KickID;
		}
		else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
		{
			if(!g_Config.m_SvVoteSpectate)
			{
				SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
				return;
			}

			int SpectateID = str_toint(pMsg->m_Value);
			if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientID, "Invalid client id to move");
				return;
			}
			if(SpectateID == ClientID)
			{
				SendChatTarget(ClientID, "You can't move yourself");
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
			m_VoteClientID = SpectateID;
		}

		if(aCmd[0])
		{
			SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
			StartVote(aDesc, aCmd, pReason);
			pPlayer->m_Vote = 1;
			pPlayer->m_VotePos = m_VotePos = 1;
			m_VoteCreator = ClientID;
			pPlayer->m_LastVoteCall = Now;
		}
	}
	else if(MsgID == NETMSGTYPE_CL_VOTE)
	{
		if(!m_VoteCloseTime)
			return;

		if(pPlayer->m_Vote == 0)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
			if(!pMsg->m_Vote)
				return;

			pPlayer->m_Vote = pMsg->m_Vote;
			pPlayer->m_VotePos = ++m_VotePos;
			m_VoteUpdate = true;
		}
	}
	else if(MsgID == NETMSGTYPE_CL_SETTEAM && m_pController->IsTeamChangeAllowed())
	{
		CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

		if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			SendBroadcast("Teams are locked", ClientID);
			return;
		}

		if(pPlayer->m_TeamChangeTick > Server()->Tick())
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
			SendBroadcast(aBuf, ClientID);
			return;
		}

		// Switch team on given client and kill/respawn him
		if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
		{
			if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
					m_VoteUpdate = true;
				m_pController->DoTeamChange(pPlayer, pMsg->m_Team);
				pPlayer->m_TeamChangeTick = Server()->Tick();
			}
			else
				SendBroadcast("Teams must be balanced, please join other team", ClientID);
		}
		else
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
			SendBroadcast(aBuf, ClientID);
		}
	}
	else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
	{
		CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		pPlayer->m_LastSetSpectatorMode = Server()->Tick();
		if(!pPlayer->SetSpectatorID(pMsg->m_SpectatorID))
			SendChatTarget(ClientID, "Invalid spectator id used");
	}
	else if (MsgID == NETMSGTYPE_CL_STARTINFO)
	{
		if(pPlayer->m_IsReadyToEnter)
			return;

		CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set start infos
		Server()->SetClientName(ClientID, pMsg->m_pName);
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);

		for(int p = 0; p < 6; p++)
		{
			str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], 24);
			pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
			pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
		}

		m_pController->OnPlayerInfoChange(pPlayer);

		// send vote options
		CNetMsg_Sv_VoteClearOptions ClearMsg;
		Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

		CNetMsg_Sv_VoteOptionListAdd OptionMsg;
		int NumOptions = 0;
		OptionMsg.m_pDescription0 = "";
		OptionMsg.m_pDescription1 = "";
		OptionMsg.m_pDescription2 = "";
		OptionMsg.m_pDescription3 = "";
		OptionMsg.m_pDescription4 = "";
		OptionMsg.m_pDescription5 = "";
		OptionMsg.m_pDescription6 = "";
		OptionMsg.m_pDescription7 = "";
		OptionMsg.m_pDescription8 = "";
		OptionMsg.m_pDescription9 = "";
		OptionMsg.m_pDescription10 = "";
		OptionMsg.m_pDescription11 = "";
		OptionMsg.m_pDescription12 = "";
		OptionMsg.m_pDescription13 = "";
		OptionMsg.m_pDescription14 = "";
		CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
		while(pCurrent)
		{
			switch(NumOptions++)
			{
			case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
			case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
			case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
			case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
			case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
			case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
			case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
			case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
			case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
			case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
			case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
			case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
			case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
			case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
			case 14:
				{
					OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
					OptionMsg.m_NumOptions = NumOptions;
					Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
					OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
					NumOptions = 0;
					OptionMsg.m_pDescription1 = "";
					OptionMsg.m_pDescription2 = "";
					OptionMsg.m_pDescription3 = "";
					OptionMsg.m_pDescription4 = "";
					OptionMsg.m_pDescription5 = "";
					OptionMsg.m_pDescription6 = "";
					OptionMsg.m_pDescription7 = "";
					OptionMsg.m_pDescription8 = "";
					OptionMsg.m_pDescription9 = "";
					OptionMsg.m_pDescription10 = "";
					OptionMsg.m_pDescription11 = "";
					OptionMsg.m_pDescription12 = "";
					OptionMsg.m_pDescription13 = "";
					OptionMsg.m_pDescription14 = "";
				}
			}
			pCurrent = pCurrent->m_pNext;
		}
		if(NumOptions > 0)
		{
			OptionMsg.m_NumOptions = NumOptions;
			Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			NumOptions = 0;
		}

		// send tuning parameters to client
		SendTuningParams(ClientID);

		// client is ready to enter
		pPlayer->m_IsReadyToEnter = true;
		CNetMsg_Sv_ReadyToEnter m;
		Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
	}
	else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
	{
		CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		pPlayer->m_LastEmote = Server()->Tick();

		SendEmoticon(ClientID, pMsg->m_Emoticon);
	}
	else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
	{
		if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()/2 > Server()->Tick())
			return;

		pPlayer->m_LastKill = Server()->Tick();
		pPlayer->KillCharacter(WEAPON_SELF);
		pPlayer->m_RespawnTick = Server()->Tick();
	}
	else if (MsgID == NETMSGTYPE_CL_READYCHANGE)
	{
		if(pPlayer->m_LastReadyChange && pPlayer->m_LastReadyChange+Server()->TickSpeed()*1 > Server()->Tick())
			return;

		pPlayer->m_LastReadyChange = Server()->Tick();
		m_pController->OnPlayerReadyChange(pPlayer);
	}
	else if (MsgID == NETMSGTYPE_CL_ISRACE)
	{
		pPlayer->m_IsUsingRaceClient = true;
		
		if(!g_Config.m_SvShowTimes)
			return;

		SendRecord(ClientID);

		// send time of all players
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && Score()->PlayerData(i)->m_CurTime > 0)
			{
				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%.0f", Score()->PlayerData(i)->m_CurTime*1000.0f); // damn ugly but the only way i know to do it
				int TimeToSend;
				sscanf(aBuf, "%d", &TimeToSend);
				CNetMsg_Sv_PlayerTime Msg;
				Msg.m_Time = TimeToSend;
				Msg.m_ClientID = i;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
			}
		}
	}
	else if (MsgID == NETMSGTYPE_CL_RACESHOWOTHERS)
	{
		if(!g_Config.m_SvShowOthers && !Server()->IsAuthed(ClientID))
			return;
				
		if(pPlayer->m_Last_ShowOthers && pPlayer->m_Last_ShowOthers+Server()->TickSpeed()/2 > Server()->Tick())
			return;
		
		pPlayer->m_Last_ShowOthers = Server()->Tick();
		
		CNetMsg_Cl_RaceShowOthers *pMsg = (CNetMsg_Cl_RaceShowOthers *)pRawMsg;
		
		pPlayer->m_ShowOthers = (bool)pMsg->m_Active;
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments())
		pSelf->m_pController->DoPause(clamp(pResult->GetInteger(0), -1, 1000));
	else
		pSelf->m_pController->DoPause(pSelf->m_pController->IsGamePaused() ? 0 : IGameController::TIMER_INFINITE);
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(clamp(pResult->GetInteger(0), -1, 1000));
	else
		pSelf->m_pController->DoWarmup(0);
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientID], Team);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[i], Team, false);
}

void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;

	int rnd = 0;
	int PlayerTeam = 0;
	int aPlayer[MAX_CLIENTS];

	for(int i = 0; i < MAX_CLIENTS; i++)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			aPlayer[PlayerTeam++]=i;

	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");

	//creating random permutation
	for(int i = PlayerTeam; i > 1; i--)
	{
		rnd = rand() % i;
		int tmp = aPlayer[rnd];
		aPlayer[rnd] = aPlayer[i-1];
		aPlayer[i-1] = tmp;
	}
	//uneven Number of Players?
	rnd = PlayerTeam % 2 ? rand() % 2 : 0;

	for(int i = 0; i < PlayerTeam; i++)
		pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[aPlayer[i]], i < (PlayerTeam+rnd)/2 ? TEAM_RED : TEAM_BLUE, false); 
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
}

void CGameContext::ConForceTeamBalance(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ForceTeamBalance();
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::ConKillPl(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	if(!pSelf->m_apPlayers[CID])
		return;
	
	pSelf->m_apPlayers[CID]->KillCharacter(WEAPON_GAME);
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "%s Killed by admin", pSelf->Server()->ClientName(CID));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
}

void CGameContext::ConTeleport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID1 = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int CID2 = clamp(pResult->GetInteger(1), 0, (int)MAX_CLIENTS-1);
	if(pSelf->m_apPlayers[CID1] && pSelf->m_apPlayers[CID2])
	{
		CCharacter* pChr = pSelf->GetPlayerChar(CID1);
		if(pChr)
		{
			pChr->GetCore()->m_Pos = pSelf->m_apPlayers[CID2]->m_ViewPos;
			pSelf->RaceController()->m_aRace[CID1].m_RaceState = CGameControllerRACE::RACE_FINISHED;
		}
		else
			pSelf->m_apPlayers[CID1]->m_ViewPos = pSelf->m_apPlayers[CID2]->m_ViewPos;
	}
}

void CGameContext::ConTeleportTo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	if(pSelf->m_apPlayers[CID])
	{
		CCharacter* pChr = pSelf->GetPlayerChar(CID);
		vec2 TelePos = vec2(pResult->GetInteger(1), pResult->GetInteger(2));
		if(pChr)
		{
			pChr->GetCore()->m_Pos = TelePos;
			pSelf->RaceController()->m_aRace[CID].m_RaceState = CGameControllerRACE::RACE_FINISHED;
		}
		else
			pSelf->m_apPlayers[CID]->m_ViewPos = TelePos;
	}
}

void CGameContext::ConGetPos(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	if(pSelf->m_apPlayers[CID])
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s pos: %d @ %d", pSelf->Server()->ClientName(CID), (int)pSelf->m_apPlayers[CID]->m_ViewPos.x, (int)pSelf->m_apPlayers[CID]->m_ViewPos.y);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Race", aBuf);
	}
}

#if defined(CONF_TEERACE)
void CGameContext::ConPing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LastPing = -1;
}

void CGameContext::ConMaplist(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pWebapp)
		return;

	int MapListSize = pSelf->m_pWebapp->GetMapCount();
	int Page = pResult->NumArguments() ? clamp(pResult->GetInteger(0), 0, (int)(MapListSize/21)) : 0;
	int Start = max(0, MapListSize - 20*(Page+1));
	for(int i = Start; i < Start+20; i++)
	{
		CServerWebapp::CMapInfo *pMapInfo = pSelf->m_pWebapp->GetMap(i);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%d. %s by %s", i, pMapInfo->m_aName, pMapInfo->m_aAuthor);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Teerace", aBuf);
	}
}

void CGameContext::ConUpdateMapVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pWebapp)
		return;

	int ID = clamp(pResult->GetInteger(0), 0, pSelf->m_pWebapp->GetMapCount()-1);
	CServerWebapp::CMapInfo *pMapInfo = pSelf->m_pWebapp->GetMap(ID);
	char aVoteDescription[128];
	if(str_find(g_Config.m_WaVoteDescription, "%s"))
		str_format(aVoteDescription, sizeof(aVoteDescription), g_Config.m_WaVoteDescription, pMapInfo->m_aName);
	else
		str_format(aVoteDescription, sizeof(aVoteDescription), "change map to %s", pMapInfo->m_aName);

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(aVoteDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", aVoteDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	char aCommand[128];
	str_format(aCommand, sizeof(aCommand), "sv_map %s", pMapInfo->m_aName);
	int Len = str_length(aCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, aVoteDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, aCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// write it to the config
	IOHANDLE File = pSelf->Server()->Storage()->OpenFile(pSelf->Server()->GetConfigFilename(), IOFLAG_UPDATE, IStorage::TYPE_ALL);
	if(!File)
	{
		str_format(aBuf, sizeof(aBuf), "failed to save vote option to config file %s", pSelf->Server()->GetConfigFilename());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// seek to the end
	io_seek(File, 0, IOSEEK_END);
	str_format(aBuf, sizeof(aBuf), "\nadd_vote \"%s\" \"%s\"", aVoteDescription, aCommand);
	io_write(File, aBuf, str_length(aBuf));
	io_close(File);
}
#endif

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");
	Console()->Register("force_teambalance", "", CFGFLAG_SERVER, ConForceTeamBalance, this, "Force team balance");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
	
	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
	
	// race commands
	Console()->Register("teleport", "ii", CFGFLAG_SERVER, ConTeleport, this, "Teleport ID 1 to ID 2");
	Console()->Register("teleport_to", "iii", CFGFLAG_SERVER, ConTeleportTo, this, "Teleport ID to (Pos X ; Pos Y)");
	Console()->Register("get_pos", "i", CFGFLAG_SERVER, ConGetPos, this, "Retrun the position of a player");
	Console()->Register("kill_pl", "i", CFGFLAG_SERVER, ConKillPl, this, "Kill a character");

#if defined(CONF_TEERACE)
	Console()->Register("ping", "", CFGFLAG_SERVER, ConPing, this, "Checks if the webapp is online");
	Console()->Register("maplist", "?i", CFGFLAG_SERVER, ConMaplist, this, "Shows the current map list with map ID's");
	Console()->Register("update_map_votes", "i", CFGFLAG_SERVER, ConUpdateMapVote, this, "Updates the map votes and stores it into the config");
#endif
}

void CGameContext::ChatConInfo(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Race mod %s (C)Rajh, Redix and Sushi", RACE_VERSION);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
#if defined(CONF_TEERACE)
	str_format(aBuf, sizeof(aBuf), "Please visit 'http://%s/about/' for more information about teerace.", g_Config.m_WaWebappIp);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
#endif
}

void CGameContext::ChatConTop5(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	if(!g_Config.m_SvShowTimes)
	{
		pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "Showing the Top5 is not allowed on this server.");
		return;
	}

	if(pResult->NumArguments() > 0)
		pSelf->Score()->ShowTop5(pSelf->m_ChatConsoleClientID,  max(1, pResult->GetInteger(0)));
	else
		pSelf->Score()->ShowTop5(pSelf->m_ChatConsoleClientID);
}

void CGameContext::ChatConRank(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	if(g_Config.m_SvShowTimes && pResult->NumArguments() > 0)
	{
		char aStr[256];
		str_copy(aStr, pResult->GetString(0), sizeof(aStr));
		
		// strip trailing spaces
		int i = str_length(aStr);
		while(i >= 0)
		{
			if(aStr[i] < 0 || aStr[i] > 32)
				break;
			aStr[i] = 0;
			i--;
		}

		pSelf->Score()->ShowRank(pSelf->m_ChatConsoleClientID, aStr, true);
	}
	else
		pSelf->Score()->ShowRank(pSelf->m_ChatConsoleClientID, pSelf->Server()->ClientName(pSelf->m_ChatConsoleClientID));
}

void CGameContext::ChatConShowOthers(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	if(!g_Config.m_SvShowOthers && !pSelf->Server()->IsAuthed(pSelf->m_ChatConsoleClientID))
	{
		pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "This command is not allowed on this server.");
		return;
	}
	
	if(pSelf->m_apPlayers[pSelf->m_ChatConsoleClientID]->m_IsUsingRaceClient)
		pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "Please use the settings to switch this option.");
	else
		pSelf->m_apPlayers[pSelf->m_ChatConsoleClientID]->m_ShowOthers = !pSelf->m_apPlayers[pSelf->m_ChatConsoleClientID]->m_ShowOthers;
}

void CGameContext::ChatConCmdlist(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "---Command List---");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/info\" information about the mod");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/rank\" shows your rank");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/rank NAME\" shows the rank of a specific player");
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/top5 X\" shows the top 5");
#if defined(CONF_TEERACE)
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/mapinfo\" shows infos about the map");
#endif
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "\"/show_others\" show other players?");
}

#if defined(CONF_TEERACE)
void CGameContext::ChatConMapInfo(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	if(!pSelf->m_pWebapp)
	{
		pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "This server does not use the webapp.");
		return;
	}

	if(pSelf->m_pWebapp->CurrentMap()->m_ID < 0)
	{
		pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "This map is not a teerace map.");
		return;
	}

	char aBuf[256];
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "----------- Mapinfo -----------");
	str_format(aBuf, sizeof(aBuf), "Name: %s", g_Config.m_SvMap);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	str_format(aBuf, sizeof(aBuf), "Author: %s", pSelf->m_pWebapp->CurrentMap()->m_aAuthor);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	str_format(aBuf, sizeof(aBuf), "URL: http://%s%s", g_Config.m_WaWebappIp, pSelf->m_pWebapp->CurrentMap()->m_aURL);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	str_format(aBuf, sizeof(aBuf), "Finished runs: %d", pSelf->m_pWebapp->CurrentMap()->m_RunCount);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	pSelf->ChatConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", "-------------------------------");
}
#endif

void CGameContext::InitChatConsole()
{
	m_pChatConsole = CreateConsole(CFGFLAG_SERVERCHAT);
	m_ChatConsoleClientID = -1;

	ChatConsole()->RegisterPrintCallback(IConsole::OUTPUT_LEVEL_STANDARD, SendChatResponse, this);
	ChatConsole()->Register("info", "", CFGFLAG_SERVERCHAT, ChatConInfo, this, "");
	ChatConsole()->Register("top5", "?i", CFGFLAG_SERVERCHAT, ChatConTop5, this, "");
	ChatConsole()->Register("rank", "?r", CFGFLAG_SERVERCHAT, ChatConRank, this, "");
	ChatConsole()->Register("show_others", "", CFGFLAG_SERVERCHAT, ChatConShowOthers, this, "");
	ChatConsole()->Register("cmdlist", "", CFGFLAG_SERVERCHAT, ChatConCmdlist, this, "");

#if defined(CONF_TEERACE)
	ChatConsole()->Register("mapinfo", "", CFGFLAG_SERVERCHAT, ChatConMapInfo, this, "");
#endif
}

void CGameContext::SendChatResponse(const char *pLine, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	if(pSelf->m_ChatConsoleClientID == -1)
		return;

	static volatile int ReentryGuard = 0;
	if(ReentryGuard)
		return;
	ReentryGuard++;

	while(*pLine && *pLine != ' ')
		pLine++;
	if(*pLine && *(pLine + 1))
		pSelf->SendChatTarget(pSelf->m_ChatConsoleClientID, pLine + 1);

	ReentryGuard--;
}

void CGameContext::OnInit()
{
	// init everything
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	if(g_Config.m_SvLoadMapDefaults)
		LoadMapSettings();

	InitChatConsole();

	// race one and only gametype
	/*if(str_comp_nocase(g_Config.m_SvGametype, "mod") == 0)
		m_pController = new CGameControllerMOD(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "ctf") == 0)
		m_pController = new CGameControllerCTF(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "lms") == 0)
		m_pController = new CGameControllerLMS(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "sur") == 0)
		m_pController = new CGameControllerSUR(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "tdm") == 0)
		m_pController = new CGameControllerTDM(this);
	else*/

	if(str_find_nocase(g_Config.m_SvGametype, "cap"))
		m_pController = new CGameControllerFC(this);
	else
		m_pController = new CGameControllerRACE(this);

	RaceController()->InitTeleporter();

#if defined(CONF_TEERACE)
	// create webapp object
	if(str_comp(g_Config.m_SvScore, "web") == 0 && !m_pWebapp)
		m_pWebapp = new CServerWebapp(this);
	else if(str_comp(g_Config.m_SvScore, "web") != 0 && m_pWebapp)
	{
		delete m_pWebapp;
		m_pWebapp = 0;
	}

	if(m_pWebapp)
		m_pWebapp->OnInit();
#endif

	// delete old score object
	if(m_pScore)
		delete m_pScore;

	// create score object
	if(str_comp(g_Config.m_SvScore, "file") == 0)
		m_pScore = new CFileScore(this);
#if defined(CONF_SQL)
	else if(str_comp(g_Config.m_SvScore, "mysql") == 0)
		m_pScore = new CSqlScore(this);
#endif
#if defined(CONF_TEERACE)
	else if(str_comp(g_Config.m_SvScore, "web") == 0)
		m_pScore = new CWebappScore(this);
#endif
	else
		m_pScore = new CFileScore(this);

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
			OnClientConnected(MAX_CLIENTS-i-1, true);
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::LoadMapSettings()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	CMapItemInfo *pItem = (CMapItemInfo *)pMap->FindItem(MAPITEMTYPE_INFO, 0);
	if(pItem && pItem->m_Settings > -1)
	{
		// load settings
		if(pItem->m_Settings > -1)
		{
			int Size = pMap->GetUncompressedDataSize(pItem->m_Settings);
			char *pBuf = new char[Size];
			mem_zero(pBuf, Size);
			mem_copy(pBuf, pMap->GetData(pItem->m_Settings), Size);
			char *pTmp = pBuf;
			int Index = 0;
			while(Index < Size)
			{
				int StrSize = str_length(pTmp);
				Console()->ExecuteLine(pTmp);
				pTmp += StrSize+1;
				Index += StrSize+1;
			}
			delete[] pBuf;
		}
	}
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CNetObj_De_TuneParams *pTuneParams = static_cast<CNetObj_De_TuneParams *>(Server()->SnapNewItem(NETOBJTYPE_DE_TUNEPARAMS, 0, sizeof(CNetObj_De_TuneParams)));
		if(!pTuneParams)
			return;

		mem_copy(pTuneParams->m_aTuneParams, &m_Tuning, sizeof(pTuneParams->m_aTuneParams));
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReadyToEnter ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->GetGameType() ? m_pController->GetGameType() : ""; }
int CmaskRace(CGameContext *pGameServer, int Owner)
{
	int Mask = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pGameServer->m_apPlayers[i] && (pGameServer->m_apPlayers[i]->m_ShowOthers || i == Owner))
			Mask = Mask|(1<<i);
	}
	return Mask;
}

const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }
