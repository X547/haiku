/*
 * Copyright 2001-2019, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef SERVER_PICTURE_H
#define SERVER_PICTURE_H


#include <DataIO.h>

#include <AutoDeleter.h>
#include <ObjectList.h>
#include <PictureDataWriter.h>
#include <Referenceable.h>


class BFile;
class Canvas;
class ServerApp;
class ServerFont;
class View;

namespace BPrivate {
	class LinkReceiver;
	class PortLink;
}
class BList;


enum PictureStateField {
	PictureState_penLocation,
	PictureState_penSize,
	PictureState_lineMode,
	PictureState_pattern,
	PictureState_drawingMode,
	PictureState_blendingMode,
	PictureState_scale,
	PictureState_highColor,
	PictureState_lowColor,
	PictureState_origin,
	PictureState_clip,

	PictureState_fillRule,
	PictureState_transform,

	PictureState_font,
};

enum PictureFontStateField {
	PictureFontState_fontStyle,
	PictureFontState_size,
	PictureFontState_encoding,
	PictureFontState_shear,
	PictureFontState_rotation,
	PictureFontState_falseBoldWidth,
	PictureFontState_spacing,
	PictureFontState_bpp,
	PictureFontState_flags,
	PictureFontState_face,
};

static const uint32 kInitialPictureStateMask
	= (1U << PictureState_penLocation)
	| (1U << PictureState_penSize)
	| (1U << PictureState_lineMode)
	| (1U << PictureState_pattern)
	| (1U << PictureState_drawingMode)
	| (1U << PictureState_blendingMode)
	| (1U << PictureState_scale)
	| (1U << PictureState_highColor)
	| (1U << PictureState_lowColor)
	| (1U << PictureState_origin)
	| (1U << PictureState_clip)
	| (1U << PictureState_fillRule)
	| (1U << PictureState_transform)
	| (1U << PictureState_font);

static const uint32 kInitialPictureFontStateMask
	= (1U << PictureFontState_fontStyle)
	| (1U << PictureFontState_size)
	| (1U << PictureFontState_encoding)
	| (1U << PictureFontState_shear)
	| (1U << PictureFontState_rotation)
	| (1U << PictureFontState_falseBoldWidth)
	| (1U << PictureFontState_spacing)
	| (1U << PictureFontState_bpp)
	| (1U << PictureFontState_flags)
	| (1U << PictureFontState_face);


class ServerPicture : public BReferenceable, public PictureDataWriter {
public:
								ServerPicture();
								ServerPicture(const ServerPicture& other);
								ServerPicture(const char* fileName,
									int32 offset);
	virtual						~ServerPicture();

			int32				Token() { return fToken; }
			bool				SetOwner(ServerApp* owner);
			ServerApp*			Owner() const { return fOwner; }

			bool				ReleaseClientReference();

			void				EnterStateChange();
			void				ExitStateChange();

	inline	void				ChangeStateField(uint32 field);
	inline	void				ChangeFontStateField(uint32 field);
	inline	void				ResetStateFields();
			void				SyncState(Canvas* canvas);

			void				Play(Canvas* target);

			void 				PushPicture(ServerPicture* picture);
			ServerPicture*		PopPicture();

			void				AppendPicture(ServerPicture* picture);
			int32				NestPicture(ServerPicture* picture);

			off_t				DataLength() const;

			status_t			ImportData(BPrivate::LinkReceiver& link);
			status_t			ExportData(BPrivate::PortLink& link);

private:
	friend class PictureBoundingBoxPlayer;

			typedef BObjectList<ServerPicture> PictureList;

			int32				fToken;
			ObjectDeleter<BFile>
								fFile;
			ObjectDeleter<BPositionIO>
								fData;
			ObjectDeleter<PictureList>
								fPictures;
			BReference<ServerPicture>
								fPushed;
			ServerApp*			fOwner;

			uint32				fChangedStateMask;
			uint32				fChangedFontStateMask;

private:
			void				SyncFontState(const ServerFont& font);
};


void
ServerPicture::ChangeStateField(uint32 field)
{
	fChangedStateMask |= 1U << field;
}


void
ServerPicture::ChangeFontStateField(uint32 field)
{
	fChangedFontStateMask |= 1U << field;
}


void
ServerPicture::ResetStateFields()
{
	fChangedStateMask = 0;
	fChangedFontStateMask = 0;
}


#endif	// SERVER_PICTURE_H
