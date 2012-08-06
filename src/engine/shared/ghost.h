/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_GHOST_H
#define ENGINE_SHARED_GHOST_H

#include <base/tl/array.h>
#include <engine/shared/protocol.h>
#include <engine/ghost.h>

class CGhostRecorder : public IGhostRecorder
{
	IOHANDLE m_File;
	class IConsole *m_pConsole;
	
	int m_AddedInfo;
	int m_NumShots;
	
	void WriteData();
	
	CGhostCharacter m_aInfoBuf[500];
	
public:	
	CGhostRecorder();
	
	int Start(class IStorage *pStorage, class IConsole *pConsole, const char *pFilename, const char *pMap, unsigned MapCrc, const char *pName, char aaSkinName[6][24], int *paUseCustomColor, int *paColors);
	
	int Stop(float Time=0.0f);
	void AddInfos(CGhostCharacter *pPlayer);

	bool IsRecording() const { return m_File != 0; }
};

#endif
