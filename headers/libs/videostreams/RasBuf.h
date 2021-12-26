#ifndef _RASBUF_H_
#define _RASBUF_H_


#include <SupportDefs.h>
#include <Point.h>
#include <Rect.h>


template <typename Color>
struct _EXPORT RasBuf {
	Color* colors;
	int32 stride, width, height;

	void Clear(Color c);
	RasBuf<Color> Clip(int x, int y, int w, int h) const;
	RasBuf<Color> Clip2(int l, int t, int r, int b) const;
	void Blit(RasBuf<Color> src, BPoint pos);
	void BlitRgb(RasBuf<Color> src, BPoint pos);
};

typedef RasBuf<uint8>  RasBuf8;
typedef RasBuf<uint32> RasBuf32;

template <typename Color>
struct _EXPORT RasBufOfs: public RasBuf<Color> {
	BPoint pos; // relative to dst
	
	RasBufOfs() {}
	RasBufOfs(const RasBuf<Color> &src) {*this = src;}
	RasBufOfs(const RasBufOfs<Color> &src) {*this = src;}
	
	RasBufOfs &operator =(const RasBuf<Color> &src) {this->colors = src.colors; this->stride = src.stride; this->width = src.width; this->height = src.height; pos.x = 0; pos.y = 0; return *this;}
	RasBufOfs &operator =(const RasBufOfs<Color> &src) {this->colors = src.colors; this->stride = src.stride; this->width = src.width; this->height = src.height; pos = src.pos; return *this;}

	RasBufOfs operator +(BPoint ofs) const {RasBufOfs res = *this; res.pos += ofs; return res;}
	RasBufOfs operator -(BPoint ofs) const {RasBufOfs res = *this; res.pos -= ofs; return res;}

	BRect Frame()
	{
		return BRect(0, 0, this->width - 1, this->height - 1).OffsetByCopy(pos);
	}

	RasBufOfs<Color> ClipOfs(BRect rc) const;
	
	void Blit(RasBufOfs<Color> src);
	void BlitRgb(RasBufOfs<Color> src);
};

typedef RasBufOfs<uint8_t>  RasBufOfs8;
typedef RasBufOfs<uint32_t> RasBufOfs32;


#endif	// _RASBUF_H_
