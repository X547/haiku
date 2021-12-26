#include "RasBuf.h"

#include <string.h>
#include <algorithm>

template <typename Color>
RasBuf<Color> RasBuf<Color>::Clip(int x, int y, int w, int h) const
{
	RasBuf<Color> vb = *this;
	if (x < 0) {w += x; x = 0;}
	if (y < 0) {h += y; y = 0;}
	if (x + w > vb.width) {w = vb.width - x;}
	if (y + h > vb.height) {h = vb.height - y;}
	if (w > 0 && h > 0) {
		vb.colors += y*vb.stride + x;
		vb.width = w;
		vb.height = h;
	} else {
		vb.colors = 0;
		vb.width = 0;
		vb.height = 0;
	}
	return vb;
}

template <typename Color>
RasBuf<Color> RasBuf<Color>::Clip2(int l, int t, int r, int b) const
{
	return Clip(l, t, r - l, b - t);
}


template <typename Color>
void RasBuf<Color>::Clear(Color c)
{
	RasBuf<Color> vb = *this;
	vb.stride -= vb.width;
	for (; vb.height > 0; vb.height--) {
		for (int x = 0; x < vb.width; x++) {
			*vb.colors = c;
			vb.colors++;
		}
		vb.colors += vb.stride;
	}
}


template <typename Color>
void RasBuf<Color>::Blit(RasBuf<Color> src, BPoint pos)
{
	int dstW = this->width, dstH = this->height;
	RasBuf<Color> dst = this->Clip(pos.x, pos.y, src.width, src.height);
	src = src.Clip(-pos.x, -pos.y, dstW, dstH);
	for (; dst.height > 0; dst.height--) {
		memcpy(dst.colors, src.colors, dst.width*sizeof(*dst.colors));
		dst.colors += dst.stride;
		src.colors += src.stride;
	}
}


template <typename Color>
void RasBuf<Color>::BlitRgb(RasBuf<Color> src, BPoint pos)
{
	int dstW = this->width, dstH = this->height;
	RasBuf<Color> dst = this->Clip(pos.x, pos.y, src.width, src.height);
	src = src.Clip(-pos.x, -pos.y, dstW, dstH);
	dst.stride -= dst.width;
	src.stride -= src.width;
	for (; dst.height > 0; dst.height--) {
		for (pos.x = dst.width; pos.x > 0; pos.x--) {
			uint32_t dc = *dst.colors;
			uint32_t sc = *src.colors;
			uint32_t a = sc >> 24;
			if (a != 0) {
				*dst.colors =
					((((dc >>  0) % 0x100)*(0xff - a)/0xff + ((sc >>  0) % 0x100)*a/0xff) <<  0) +
					((((dc >>  8) % 0x100)*(0xff - a)/0xff + ((sc >>  8) % 0x100)*a/0xff) <<  8) +
					((((dc >> 16) % 0x100)*(0xff - a)/0xff + ((sc >> 16) % 0x100)*a/0xff) << 16) +
					(dc & 0xff000000);
			}
			dst.colors++;
			src.colors++;
		}
		dst.colors += dst.stride;
		src.colors += src.stride;
	}
}


template class RasBuf<uint8>;
template class RasBuf<uint32>;



template <typename Color>
RasBufOfs<Color> RasBufOfs<Color>::ClipOfs(BRect rc) const
{
	RasBufOfs<Color> vb = this->Clip(rc.left + pos.x, rc.top + pos.y, rc.Width() + 1, rc.Height() + 1);
	vb.pos.x = std::min(pos.x, -rc.left);
	vb.pos.y = std::min(pos.y, -rc.top);
	return vb;
}


template <typename Color>
void RasBufOfs<Color>::Blit(RasBufOfs<Color> src)
{
	RasBuf<Color>::Blit(src, pos - src.pos);
}

template <typename Color>
void RasBufOfs<Color>::BlitRgb(RasBufOfs<Color> src)
{
	RasBuf<Color>::BlitRgb(src, pos - src.pos);
}


template class RasBufOfs<uint8>;
template class RasBufOfs<uint32>;
