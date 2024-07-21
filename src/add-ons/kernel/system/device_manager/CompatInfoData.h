#pragma once


#include <SupportDefs.h>


struct CompatInfoData {
	const char* addonPath;
	const uint8* data;
};


extern const CompatInfoData kCompatInfoData[];
extern const int32 kCompatInfoDataLen;
