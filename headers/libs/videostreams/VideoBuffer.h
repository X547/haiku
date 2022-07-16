#ifndef _VIDEOBUFFER_H_
#define _VIDEOBUFFER_H_


#include <OS.h>
#include <InterfaceDefs.h>
#include <AutoDeleter.h>


enum PresentEffect {
	presentEffectNone,
	presentEffectSwap,
	presentEffectCopy,
};

enum BufferRefKind {
	bufferRefArea,
	bufferRefGpu,
};

struct BufferRef {
	uint64 offset, size;
	BufferRefKind kind;
	union {
		struct {
			area_id id;
		} area;
		struct {
			int32 id;
			team_id team;
		} gpu;
	};
};

struct BufferFormat {
	int32 bytesPerRow;
	int32 width, height;
	color_space colorSpace;
};

struct VideoBuffer
{
	int32 id; // index in SwapChain.buffers
	BufferRef ref;
	BufferFormat format;
};


struct SwapChainSpec {
	size_t size;
	PresentEffect presentEffect;
	size_t bufferCnt;
	BufferRefKind kind;
	int32 width, height;
	color_space colorSpace;
};


struct SwapChain {
	size_t size;
	PresentEffect presentEffect;
	uint32 bufferCnt;
	VideoBuffer* buffers;

	static SwapChain *New(uint32 bufferCnt);
	status_t Copy(ObjectDeleter<SwapChain> &dst) const;
	static status_t NewFromMessage(ObjectDeleter<SwapChain> &dst, const BMessage& msg, const char *name);
	status_t ToMessage(BMessage& msg, const char *name) const;
};


#endif	// _VIDEOBUFFER_H_
