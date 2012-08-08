/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CAMERA_H
#define GAME_CLIENT_COMPONENTS_CAMERA_H
#include <base/vmath.h>
#include <game/client/component.h>

class CCamera : public CComponent
{
public:
	enum
	{
		POS_START=0,
		POS_INTERNET,
		POS_LAN,
		POS_FAVORITES,
		POS_DEMOS,
		POS_SETTINGS,

		NUM_POS,
	};

	vec2 m_Center;
	vec2 m_MenuCenter;
	vec2 m_RotationCenter;
	float m_Zoom;
	bool m_ZoomBind;

	CCamera();
	virtual void OnRender();

	void ChangePosition(int PositionNumber);
	int GetCurrentPosition();

	static void ConSetPosition(IConsole::IResult *pResult, void *pUserData);

	virtual void OnConsoleInit();
	virtual void OnStateChange(int NewState, int OldState);

	enum
	{
		CAMTYPE_UNDEFINED=-1,
		CAMTYPE_SPEC,
		CAMTYPE_PLAYER,
	};

	int m_CamType;
	vec2 m_PrevCenter;
	vec2 m_Positions[NUM_POS];
	int m_CurrentPosition;
	
	static void ConKeyZoomin(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyZoomout(IConsole::IResult *pResult, void *pUserData);
	static void ConZoom(IConsole::IResult *pResult, void *pUserData);
	static void ConZoomReset(IConsole::IResult *pResult, void *pUserData);
};

#endif
