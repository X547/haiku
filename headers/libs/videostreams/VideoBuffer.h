#ifndef _VIDEOBUFFER_H_
#define _VIDEOBUFFER_H_


#include <OS.h>
#include <InterfaceDefs.h>


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


struct BufferSpec {
	color_space colorSpace;
};


struct SwapChainSpec {
	size_t size;
	PresentEffect presentEffect;
	size_t bufferCnt;
	BufferSpec* bufferSpecs;
};


struct SwapChain {
	size_t size;
	PresentEffect presentEffect;
	uint32 bufferCnt;
	VideoBuffer* buffers;
};


#endif	// _VIDEOBUFFER_H_
