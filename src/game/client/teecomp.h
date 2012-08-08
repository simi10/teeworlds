#ifndef __TEECOMP_HPP_
#define __TEECOMP_HPP_

#include <base/vmath.h>

#include <game/client/components/skins.h>

enum {
	TC_STATS_FRAGS=1,
	TC_STATS_DEATHS=2,
	TC_STATS_SUICIDES=4,
	TC_STATS_RATIO=8,
	TC_STATS_NET=16,
	TC_STATS_FPM=32,
	TC_STATS_SPREE=64,
	TC_STATS_BESTSPREE=128,
	TC_STATS_FLAGGRABS=256,
	TC_STATS_WEAPS=512,
	TC_STATS_FLAGCAPTURES=1024,

	TC_SCORE_TITLE=1,
	TC_SCORE_COUNTRY=2,
	TC_SCORE_CLAN=4,
	TC_SCORE_PING=8,
	TC_SCORE_HIDEBORDER=16,
	TC_SCORE_NOCOLORHEADERS=32,
	TC_SCORE_HIDESEPERATOR=64,
};

class CTeecompUtils
{
public:
	static vec3 GetTeamColor(int ForTeam, int LocalTeam, int Method);
	static int GetTeamColorInt(int ForTeam, int LocalTeam, int Method, int Part=SKINPART_BODY);
	static bool GetForcedSkinPartName(int ForTeam, int LocalTeam, const char*& pSkinPartName, int Part);
	static bool GetForceDmColors(int ForTeam, int LocalTeam);
	static void ResetConfig();
	static const char* RgbToName(int rgb);
	static const char* TeamColorToName(int rgb);
};

extern char *const gs_apTeecompSkinVariables1[NUM_SKINPARTS];
extern char *const gs_apTeecompSkinVariables2[NUM_SKINPARTS];

#endif
