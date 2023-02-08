#include "VideoBufferUtils.h"
#include <Bitmap.h>
#include <Message.h>
#include <Region.h>
#include <OS.h>
#include <syscalls.h>
#include <sys/stat.h>
#include <stdio.h>

#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


status_t VideoBufferFromBitmap(VideoBuffer &buf, BBitmap &bmp)
{
	area_info info;
	CheckRet(bmp.InitCheck());
	CheckRet(bmp.Area());
	CheckRet(get_area_info(bmp.Area(), &info));
	buf.ref = BufferRef {
		.offset  = (addr_t)bmp.Bits() - (addr_t)info.address,
		.size    = (uint64)bmp.BitsLength(),
		.kind    = bufferRefArea,
		.area    = {.area = {.id = bmp.Area()}}
	};
	buf.format = BufferFormat {
		.bytesPerRow = bmp.BytesPerRow(),
		.width       = (int32)bmp.Bounds().Width() + 1,
		.height      = (int32)bmp.Bounds().Height() + 1,
		.colorSpace  = bmp.ColorSpace()
	};
	return B_OK;
}

RasBuf32 RasBufFromFromBitmap(BBitmap *bmp)
{
	if (bmp == NULL) {
		return (RasBuf32) {};
	}
	return (RasBuf32) {
		.colors = (uint32*)bmp->Bits(),
		.stride = bmp->BytesPerRow() / 4,
		.width = (int32)bmp->Bounds().Width() + 1,
		.height = (int32)bmp->Bounds().Height() + 1,
	};
}


status_t GetRegion(BMessage& msg, const char* name, BRegion*& region)
{
	if (msg.HasInt32(name)) {
		// NULL mark
		region = NULL;
		return B_OK;
	}
	BRect rect;
	CheckRet(msg.FindRect(name, 0, &rect));
	region->MakeEmpty();
	for (int32 i = 0; msg.FindRect(name, i, &rect) >= B_OK; i++) {
		region->Include(rect);
	}
	return B_OK;
}

status_t SetRegion(BMessage& msg, const char* name, const BRegion* region)
{
	if (region == NULL) {
		// NULL mark
		CheckRet(msg.AddInt32(name, 0));
		return B_OK;
	}
	if (region->CountRects() == 0) {
		CheckRet(msg.AddRect(name, BRect()));
	} else {
		for (int32 i = 0; i < region->CountRects(); i++) {
			CheckRet(msg.AddRect(name, region->RectAt(i)));
		}
	}
	return B_OK;
}


SwapChain *SwapChain::New(uint32 bufferCnt)
{
	ObjectDeleter<SwapChain> swapChain;
	uint8 *swapChainMem = (uint8*)::operator new(sizeof(SwapChain) + bufferCnt*sizeof(VideoBuffer));
	VideoBuffer *buffers = (VideoBuffer*)(swapChainMem + sizeof(SwapChain));
	memset(buffers, 0, bufferCnt*sizeof(VideoBuffer));
	swapChain.SetTo((SwapChain*)swapChainMem);
	*swapChain.Get() = {
		.size = sizeof(SwapChain),
		.bufferCnt = bufferCnt,
		.buffers = buffers
	};
	return swapChain.Detach();
}

SwapChain::~SwapChain()
{
	auto &src = *this;
	if (src.buffers == NULL) return;
	for (size_t i = 0; i < src.bufferCnt; i++) {
		switch (src.buffers[i].ref.kind) {
			case bufferRefGpu: {
				close(src.buffers[i].ref.gpu.fd); src.buffers[i].ref.gpu.fd = -1;
				close(src.buffers[i].ref.gpu.fenceFd); src.buffers[i].ref.gpu.fenceFd = -1;
				break;
			}
			default:
				break;
		}
	}
}

status_t SwapChain::Copy(ObjectDeleter<SwapChain> &dst, team_id dstTeam) const
{
	auto &src = *this;
	size_t numBytes = sizeof(SwapChain) + src.bufferCnt*sizeof(VideoBuffer);
	dst.SetTo((SwapChain*)::operator new(numBytes));
	if (!dst.IsSet()) return B_NO_MEMORY;
	memcpy(dst.Get(), &src, sizeof(SwapChain));
	dst->buffers = (VideoBuffer*)(dst.Get() + 1);
	memcpy(dst->buffers, src.buffers, src.bufferCnt*sizeof(VideoBuffer));
	for (size_t i = 0; i < src.bufferCnt; i++) {
		switch (src.buffers[i].ref.kind) {
			case bufferRefGpu: {
				dst->buffers[i].ref.gpu.fd = _kern_dup_foreign(B_CURRENT_TEAM, dstTeam, src.buffers[i].ref.gpu.fd, O_CLOEXEC);
				dst->buffers[i].ref.gpu.fenceFd = _kern_dup_foreign(B_CURRENT_TEAM, dstTeam, src.buffers[i].ref.gpu.fenceFd, O_CLOEXEC);
				break;
			}
			default:
				break;
		}
	}

	return B_OK;
}

status_t SwapChain::NewFromMessage(ObjectDeleter<SwapChain> &swapChain, const BMessage& msg, const char *name)
{
	const void* data;
	ssize_t numBytes;

	CheckRet(msg.FindData(name, B_RAW_TYPE, &data, &numBytes));
	if ((size_t)numBytes < sizeof(SwapChain)) return B_BAD_VALUE;
	if (((SwapChain*)data)->size != sizeof(SwapChain)) return B_BAD_VALUE;
	if ((size_t)numBytes != sizeof(SwapChain) + ((SwapChain*)data)->bufferCnt * sizeof(VideoBuffer)) return B_BAD_VALUE;
	swapChain.SetTo((SwapChain*)::operator new(numBytes));
	memcpy(swapChain.Get(), data, numBytes);
	swapChain->buffers = (VideoBuffer*)(swapChain.Get() + 1);

	return B_OK;
}

status_t SwapChain::ToMessage(BMessage& msg, const char *name, team_id dstTeam) const
{
	const SwapChain &swapChain = *this;
	ObjectDeleter<SwapChain> swapChainCopy;
	CheckRet(swapChain.Copy(swapChainCopy, dstTeam));
	swapChainCopy->buffers = NULL;
	CheckRet(msg.AddData(name, B_RAW_TYPE, swapChainCopy.Get(), sizeof(SwapChain) + swapChain.bufferCnt*sizeof(VideoBuffer)));

	return B_OK;
}

void DumpSwapChain(const SwapChain &swapChain)
{
	printf("swapChain: \n");
	printf("  size: %" B_PRIuSIZE "\n", swapChain.size);
	printf("  bufferCnt: %" B_PRIu32 "\n", swapChain.bufferCnt);
	printf("  buffers:\n");
	for (uint32 i = 0; i < swapChain.bufferCnt; i++) {
		const VideoBuffer &buffer = swapChain.buffers[i];
		printf("    %" B_PRIu32 "\n", i);
		switch (buffer.ref.kind) {
			case bufferRefArea: {
				printf("      ref.kind: area\n");
				printf("      ref.area.id: %" B_PRId32 "\n", buffer.ref.area.id);
				break;
			}
			case bufferRefGpu: {
				struct stat st{}, fenceSt{};
				fstat(buffer.ref.gpu.fd, &st);
				fstat(buffer.ref.gpu.fenceFd, &fenceSt);
				printf("      ref.kind: gpu\n");
				printf("      ref.gpu.fd: %" B_PRId32 " (dev: %" B_PRIu32 ", ino: %" B_PRIu32 ")\n",
					buffer.ref.gpu.fd, (uint64)st.st_dev, (uint64)st.st_ino);
				printf("      ref.gpu.fenceFd: %" B_PRId32 " (dev: %" B_PRIu32 ", ino: %" B_PRIu32 ")\n",
					buffer.ref.gpu.fenceFd, (uint64)fenceSt.st_dev, (uint64)fenceSt.st_ino);
				break;
			}
			default:
				printf("      ref.kind: ?(%d)\n", buffer.ref.kind);
		}
		printf("      ref.offset: %" B_PRIuSIZE "\n",       buffer.ref.offset);
		printf("      ref.length: %" B_PRIu64 "\n",         buffer.ref.size);
		printf("      format.bytesPerRow: %" B_PRIu32 "\n", buffer.format.bytesPerRow);
		printf("      format.width: %" B_PRIu32 "\n",       buffer.format.width);
		printf("      format.height: %" B_PRIu32 "\n",      buffer.format.height);
		printf("      format.colorSpace: %d\n",             buffer.format.colorSpace);
	}
}
