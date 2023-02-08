/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef DRAWING_ENGINE_BASE_H_
#define DRAWING_ENGINE_BASE_H_


#include <InterfaceDefs.h>

#include "HWInterface.h"


class BAffineTransform;
class BGradient;
struct escapement_delta;

class DrawState;
class ServerFont;
struct ViewLineArrayInfo;


class DrawingEngineBase : public HWInterfaceListener {
public:
							DrawingEngineBase();
	virtual					~DrawingEngineBase();

	// HWInterfaceListener interface
	virtual	void			FrameBufferChanged();

	// for "changing" hardware
			void			SetHWInterface(HWInterface* interface);

	virtual	void			SetCopyToFrontEnabled(bool enable);
			bool			CopyToFrontEnabled() const
								{ return fCopyToFront; }
	virtual	void			CopyToFront(/*const*/ BRegion& region);

	// locking
			bool			LockParallelAccess();
#if DEBUG
	virtual	bool			IsParallelAccessLocked() const;
#endif
			void			UnlockParallelAccess();

			bool			LockExclusiveAccess();
	virtual	bool			IsExclusiveAccessLocked() const;
			void			UnlockExclusiveAccess();

	// for screen shots
			ServerBitmap*	DumpToBitmap();
	virtual	status_t		ReadBitmap(ServerBitmap *bitmap, bool drawCursor,
								BRect bounds) = 0;

	// clipping for all drawing functions, passing a NULL region
	// will remove any clipping (drawing allowed everywhere)
	virtual	void			ConstrainClippingRegion(const BRegion* region) = 0;

	virtual	void			SetDrawState(const DrawState* state,
								int32 xOffset = 0, int32 yOffset = 0) = 0;

	virtual	void			SetHighColor(const rgb_color& color) = 0;
	virtual	void			SetLowColor(const rgb_color& color) = 0;
	virtual	void			SetPenSize(float size) = 0;
	virtual	void			SetStrokeMode(cap_mode lineCap, join_mode joinMode,
								float miterLimit) = 0;
	virtual void			SetFillRule(int32 fillRule) = 0;
	virtual	void			SetPattern(const struct pattern& pattern) = 0;
	virtual	void			SetDrawingMode(drawing_mode mode) = 0;
	virtual	void			SetDrawingMode(drawing_mode mode,
								drawing_mode& oldMode) = 0;
	virtual	void			SetBlendingMode(source_alpha srcAlpha,
								alpha_function alphaFunc) = 0;
	virtual	void			SetFont(const ServerFont& font) = 0;
	virtual	void			SetFont(const DrawState* state) = 0;
	virtual	void			SetTransform(const BAffineTransform& transform,
								int32 xOffset, int32 yOffset) = 0;

			void			SuspendAutoSync();
			void			Sync();

	// drawing functions
	virtual	void			CopyRegion(/*const*/ BRegion* region,
								int32 xOffset, int32 yOffset) = 0;

	virtual	void			InvertRect(BRect r) = 0;

	virtual	void			DrawBitmap(ServerBitmap* bitmap,
								const BRect& bitmapRect, const BRect& viewRect,
								uint32 options = 0) = 0;
	// drawing primitives
	virtual	void			DrawArc(BRect r, const float& angle,
								const float& span, bool filled) = 0;
	virtual	void			FillArc(BRect r, const float& angle,
								const float& span, const BGradient& gradient) = 0;

	virtual	void			DrawBezier(BPoint* pts, bool filled) = 0;
	virtual	void			FillBezier(BPoint* pts, const BGradient& gradient) = 0;

	virtual	void			DrawEllipse(BRect r, bool filled) = 0;
	virtual	void			FillEllipse(BRect r, const BGradient& gradient) = 0;

	virtual	void			DrawPolygon(BPoint* ptlist, int32 numpts,
								BRect bounds, bool filled, bool closed) = 0;
	virtual	void			FillPolygon(BPoint* ptlist, int32 numpts,
								BRect bounds, const BGradient& gradient,
								bool closed) = 0;

	// these rgb_color versions are used internally by the server
	virtual	void			StrokePoint(const BPoint& point,
								const rgb_color& color) = 0;
	virtual	void			StrokeRect(BRect rect, const rgb_color &color) = 0;
	virtual	void			FillRect(BRect rect, const rgb_color &color) = 0;
	virtual	void			FillRegion(BRegion& region, const rgb_color& color) = 0;

	virtual	void			StrokeRect(BRect rect) = 0;
	virtual	void			FillRect(BRect rect) = 0;
	virtual	void			FillRect(BRect rect, const BGradient& gradient) = 0;

	virtual	void			FillRegion(BRegion& region) = 0;
	virtual	void			FillRegion(BRegion& region,
								const BGradient& gradient) = 0;

	virtual	void			DrawRoundRect(BRect rect, float xrad,
								float yrad, bool filled) = 0;
	virtual	void			FillRoundRect(BRect rect, float xrad,
								float yrad, const BGradient& gradient) = 0;

	virtual	void			DrawShape(const BRect& bounds,
								int32 opcount, const uint32* oplist,
								int32 ptcount, const BPoint* ptlist,
								bool filled, const BPoint& viewToScreenOffset,
								float viewScale) = 0;
	virtual	void			FillShape(const BRect& bounds,
								int32 opcount, const uint32* oplist,
							 	int32 ptcount, const BPoint* ptlist,
							 	const BGradient& gradient,
							 	const BPoint& viewToScreenOffset,
								float viewScale) = 0;

	virtual	void			DrawTriangle(BPoint* points, const BRect& bounds,
								bool filled) = 0;
	virtual	void			FillTriangle(BPoint* points, const BRect& bounds,
								const BGradient& gradient) = 0;

	// these versions are used by the Decorator
	virtual	void			StrokeLine(const BPoint& start,
								const BPoint& end, const rgb_color& color) = 0;

	virtual	void			StrokeLine(const BPoint& start,
								const BPoint& end) = 0;

	virtual	void			StrokeLineArray(int32 numlines,
								const ViewLineArrayInfo* data) = 0;

	// -------- text related calls

	// returns the pen position behind the (virtually) drawn
	// string
	virtual	BPoint			DrawString(const char* string, int32 length,
								const BPoint& pt,
								escapement_delta* delta = NULL) = 0;
	virtual	BPoint			DrawString(const char* string, int32 length,
								const BPoint* offsets) = 0;

	virtual	float			StringWidth(const char* string, int32 length,
								escapement_delta* delta = NULL) = 0;

	// convenience function which is independent of graphics
	// state (to be used by Decorator or ServerApp etc)
	virtual	float			StringWidth(const char* string,
								int32 length, const ServerFont& font,
								escapement_delta* delta = NULL) = 0;

	virtual	BPoint			DrawStringDry(const char* string, int32 length,
								const BPoint& pt,
								escapement_delta* delta = NULL) = 0;
	virtual	BPoint			DrawStringDry(const char* string, int32 length,
								const BPoint* offsets) = 0;


	// software rendering backend invoked by CopyRegion() for the sorted
	// individual rects
	virtual	BRect			CopyRect(BRect rect, int32 xOffset,
								int32 yOffset) const = 0;

	virtual	void			SetRendererOffset(int32 offsetX, int32 offsetY) = 0;

protected:
			HWInterface*	fGraphicsCard;
			uint32			fAvailableHWAccleration;
			int32			fSuspendSyncLevel;
			bool			fCopyToFront;
};

#endif // DRAWING_ENGINE_BASE_H_
