/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "pickup.h"

CPickup::CPickup(CGameWorld *pGameWorld, int Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP)
{
	m_Type = Type;
	m_ProximityRadius = PickupPhysSize;

	Reset();

	GameWorld()->InsertEntity(this);
}

void CPickup::Reset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if (g_pData->m_aPickups[m_Type].m_Spawndelay > 0)
			m_SpawnTick[i] = Server()->Tick() + Server()->TickSpeed() * g_pData->m_aPickups[m_Type].m_Spawndelay;
		else
			m_SpawnTick[i] = -1;
	}
}

void CPickup::Tick()
{
	// wait for respawn
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// reset Pickups after player death
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_ResetPickups)
		{
			// respawn
			m_SpawnTick[i] = -1;
			continue;
		}
		
		if(m_SpawnTick[i] > 0)
		{
			if(Server()->Tick() > m_SpawnTick[i] && g_Config.m_SvPickupRespawn > -1)
			{
				// respawn
				m_SpawnTick[i] = -1;

				if(m_Type == PICKUP_GRENADE | m_Type == PICKUP_SHOTGUN | m_Type == PICKUP_LASER)
					GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN, CmaskOne(i));
			}
		}
	}
	
	// Check if a player intersected us
	CCharacter *apChrs[MAX_CLIENTS];
	int Num = GameServer()->m_World.FindEntities(m_Pos, 20.0f, (CEntity**)apChrs, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int j = 0; j < Num; j++)
	{
		if(apChrs[j]->IsAlive() && m_SpawnTick[apChrs[j]->GetPlayer()->GetCID()] == -1)
		{
			// player picked us up, is someone was hooking us, let them go
			int RespawnTime = -1;
			switch (m_Type)
			{
				case PICKUP_HEALTH:
					if(apChrs[j]->IncreaseHealth(1))
					{
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH, CmaskOne(apChrs[j]->GetPlayer()->GetCID()));
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;
					}
					break;
					
				case PICKUP_ARMOR:
					if(apChrs[j]->IncreaseArmor(1))
					{
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, CmaskOne(apChrs[j]->GetPlayer()->GetCID()));
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;
					}
					break;

				case PICKUP_GRENADE:
					if(apChrs[j]->GiveWeapon(WEAPON_GRENADE, 10))
					{
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_GRENADE, CmaskOne(apChrs[j]->GetPlayer()->GetCID()));
						if(apChrs[j]->GetPlayer())
							GameServer()->SendWeaponPickup(apChrs[j]->GetPlayer()->GetCID(), WEAPON_GRENADE);
					}
					break;

				case PICKUP_SHOTGUN:
					if(apChrs[j]->GiveWeapon(WEAPON_SHOTGUN, 10))
					{
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_SHOTGUN, CmaskOne(apChrs[j]->GetPlayer()->GetCID()));
						if(apChrs[j]->GetPlayer())
							GameServer()->SendWeaponPickup(apChrs[j]->GetPlayer()->GetCID(), WEAPON_SHOTGUN);
					}
					break;

				case PICKUP_LASER:
					if(apChrs[j]->GiveWeapon(WEAPON_LASER, 10))
					{
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_SHOTGUN, CmaskOne(apChrs[j]->GetPlayer()->GetCID()));
						if(apChrs[j]->GetPlayer())
							GameServer()->SendWeaponPickup(apChrs[j]->GetPlayer()->GetCID(), WEAPON_LASER);
					}
					break;

				case PICKUP_NINJA:
					{
						// activate ninja on target player
						apChrs[j]->GiveNinja();
						RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;

						// loop through all players, setting their emotes
						/*CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
						for(; pC; pC = (CCharacter *)pC->TypeNext())
						{
							if (pC != pChr)
							pC->SetEmote(EMOTE_SURPRISE, Server()->Tick() + Server()->TickSpeed());
						}*/

						apChrs[j]->SetEmote(EMOTE_ANGRY, Server()->Tick() + 1200 * Server()->TickSpeed() / 1000);
						break;
					}
						
				default:
					break;
			};

			if(RespawnTime >= 0)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "pickup player='%d:%s' item=%d/%d", apChrs[j]->GetPlayer()->GetCID(), Server()->ClientName(apChrs[j]->GetPlayer()->GetCID()), m_Type);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

				if(g_Config.m_SvPickupRespawn > -1)
					m_SpawnTick[apChrs[j]->GetPlayer()->GetCID()] = Server()->Tick() + Server()->TickSpeed() * g_Config.m_SvPickupRespawn;
				else
					m_SpawnTick[apChrs[j]->GetPlayer()->GetCID()] = 1;
			}
		}
	}
}

void CPickup::TickPaused()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_SpawnTick[i] != -1)
			++m_SpawnTick[i];
}

void CPickup::Snap(int SnappingClient)
{
	if(m_SpawnTick[SnappingClient] != -1 || NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = m_Type;
}
