#include <base/math.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include "teecomp.h"

char *const gs_apTeecompSkinVariables1[NUM_SKINPARTS] = {g_Config.m_TcForcedSkin1, g_Config.m_TcForcedSkinTattoo1, g_Config.m_TcForcedSkinDecoration1,
													g_Config.m_TcForcedSkinHands1, g_Config.m_TcForcedSkinFeet1, g_Config.m_TcForcedSkinEyes1};
char *const gs_apTeecompSkinVariables2[NUM_SKINPARTS] = {g_Config.m_TcForcedSkin2, g_Config.m_TcForcedSkinTattoo2, g_Config.m_TcForcedSkinDecoration2,
													g_Config.m_TcForcedSkinHands2, g_Config.m_TcForcedSkinFeet2, g_Config.m_TcForcedSkinEyes2};

vec3 CTeecompUtils::GetTeamColor(int ForTeam, int LocalTeam, int Method)
{
	int Color1 = g_Config.m_TcColoredTeesTeam1;
	int Color2 = g_Config.m_TcColoredTeesTeam2;

	// Team based Colors or spectating
	if(!Method || LocalTeam == -1)
	{
		if(ForTeam == 0)
			return vec3((Color1>>16)/255.0f, ((Color1>>8)&0xff)/255.0f, (Color1&0xff)/255.0f);
		return vec3((Color2>>16)/255.0f, ((Color2>>8)&0xff)/255.0f, (Color2&0xff)/255.0f);
	}

	// Enemy based Colors
	if(ForTeam == LocalTeam)
		return vec3((Color1>>16)/255.0f, ((Color1>>8)&0xff)/255.0f, (Color1&0xff)/255.0f);
	return vec3((Color2>>16)/255.0f, ((Color2>>8)&0xff)/255.0f, (Color2&0xff)/255.0f);
}

int CTeecompUtils::GetTeamColorInt(int ForTeam, int LocalTeam, int Method, int Part)
{
	int Color1 = g_Config.m_TcColoredTeesTeam1;
	int Color2 = g_Config.m_TcColoredTeesTeam2;
	if(Part == SKINPART_TATTOO)
	{
		Color1 |= 0xff000000;
		Color2 |= 0xff000000;
	}

	// Team based Colors or spectating
	if(!Method || LocalTeam == -1)
	{
		if(ForTeam == 0)
			return Color1;
		return Color2;
	}

	// Enemy based Colors
	if(ForTeam == LocalTeam)
		return Color1;
	return Color2;
}

bool CTeecompUtils::GetForcedSkinPartName(int ForTeam, int LocalTeam, const char*& pSkinPartName, int Part)
{
	// Team based Colors or spectating
	if(!g_Config.m_TcForcedSkinsMethod || LocalTeam == -1)
	{
		if(ForTeam == 0)
		{
			pSkinPartName = gs_apTeecompSkinVariables1[Part];
			return g_Config.m_TcForceSkinTeam1;
		}
		pSkinPartName = gs_apTeecompSkinVariables2[Part];
		return g_Config.m_TcForceSkinTeam2;
	}

	// Enemy based Colors
	if(ForTeam == LocalTeam)
	{
		pSkinPartName = gs_apTeecompSkinVariables1[Part];
		return g_Config.m_TcForceSkinTeam1;
	}
	pSkinPartName = gs_apTeecompSkinVariables2[Part];
	return g_Config.m_TcForceSkinTeam2;
}

bool CTeecompUtils::GetForceDmColors(int ForTeam, int LocalTeam)
{
	if(!g_Config.m_TcColoredTeesMethod || LocalTeam == -1)
	{
		if(ForTeam == 0)
			return g_Config.m_TcDmColorsTeam1;
		return g_Config.m_TcDmColorsTeam2;
	}

	if(ForTeam == LocalTeam)
		return g_Config.m_TcDmColorsTeam1;
	return g_Config.m_TcDmColorsTeam2;
}

void CTeecompUtils::ResetConfig()
{
	#define MACRO_CONFIG_INT(Name,ScriptName,Def,Min,Max,Save,Desc) g_Config.m_##Name = Def;
	#define MACRO_CONFIG_STR(Name,ScriptName,Len,Def,Save,Desc) str_copy(g_Config.m_##Name, Def, Len);
	#include "../teecomp_vars.h"
	#undef MACRO_CONFIG_INT
	#undef MACRO_CONFIG_STR
}

static vec3 RgbToHsl(vec3 rgb)
{
	float r = rgb.r;
	float g = rgb.g;
	float b = rgb.b;

	float vMin = min(min(r, g), b);
	float vMax = max(max(r, g), b);
	float dMax = vMax - vMin;

	float h = 0.0f;
	float s = 0.0f;
	float l = (vMax + vMin) / 2.0f;

	if(dMax == 0.0f)
	{
		h = 0.0f;
		s = 0.0f;
	}
	else
	{
		if(l < 0.5f)
			s = dMax / (vMax + vMin);
		else
			s = dMax / (2 - vMax - vMin);

		float dR = (((vMax - r) / 6.0f) + (dMax / 2.0f)) / dMax;
		float dG = (((vMax - g) / 6.0f) + (dMax / 2.0f)) / dMax;
		float dB = (((vMax - b) / 6.0f) + (dMax / 2.0f)) / dMax;

		if(r == vMax)
			h = dB - dG;
		else if(g == vMax)
			h = (1.0f/3.0f) + dR - dB;
		else if(b == vMax)
			h = (2.0f/3.0f) + dG - dR;

		if(h < 0.0f)
			h += 1.0f;
		if(h > 1.0f)
			h -= 1.0f;
	}

	return vec3(h*360, s, l);
}

const char* CTeecompUtils::RgbToName(int rgb)
{
	vec3 rgb_v((rgb>>16)/255.0f, ((rgb>>8)&0xff)/255.0f, (rgb&0xff)/255.0f);
	vec3 hsl = RgbToHsl(rgb_v);

	if(hsl.l < 0.2f)
		return "Black";
	if(hsl.l > 0.9f)
		return "White";
	if(hsl.s < 0.1f)
		return "Gray";
	if(hsl.h < 20)
		return "Red";
	if(hsl.h < 45)
		return "Orange";
	if(hsl.h < 70)
		return "Yellow";
	if(hsl.h < 155)
		return "Green";
	if(hsl.h < 260)
		return "Blue";
	if(hsl.h < 335)
		return "Purple";
	return "Red";
}

const char* CTeecompUtils::TeamColorToName(int rgb)
{
	vec3 rgb_v((rgb>>16)/255.0f, ((rgb>>8)&0xff)/255.0f, (rgb&0xff)/255.0f);
	vec3 hsl = RgbToHsl(rgb_v);

	if(hsl.l < 0.2f)
		return "black";
	if(hsl.l > 0.9f)
		return "white";
	if(hsl.s < 0.1f)
		return "gray";
	if(hsl.h < 20)
		return "red";
	if(hsl.h < 45)
		return "orange";
	if(hsl.h < 70)
		return "yellow";
	if(hsl.h < 155)
		return "green";
	if(hsl.h < 260)
		return "blue";
	if(hsl.h < 335)
		return "purple";
	return "red";
}
