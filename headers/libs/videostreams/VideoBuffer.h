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
	BufferRefKind kind;
	uint64 offset, size;
	union {
		struct {
			area_id area;
		} area;
		struct {
			int32 buffer;
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
	area_id area;
	size_t offset;
	int32 length;
	int32 bytesPerRow;
	int32 width, height;
	color_space colorSpace;
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
