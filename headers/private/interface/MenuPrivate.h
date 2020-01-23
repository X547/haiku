/*
 * Copyright 2006-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */

#ifndef __MENU_PRIVATE_H
#define __MENU_PRIVATE_H


#include <Menu.h>
#include <MessageRunner.h>
#include <Locker.h>

#include <AutoDeleter.h>


enum menu_track_command {
	MENU_TRACK_CMD_DONE
};

enum {
	navigationAreaTimeoutMsg = 'nvat',
};


class BBitmap;
class BMenu;
class BWindow;


namespace BPrivate {

extern const char* kEmptyMenuLabel;

class MenuPrivate {
public:
								MenuPrivate(BMenu* menu);

			menu_layout			Layout() const;
			void				SetLayout(menu_layout layout);

			void				ItemMarked(BMenuItem* item);
			void				CacheFontInfo();

			float				FontHeight() const;
			float				Ascent() const;
			BRect				Padding() const;
			void				GetItemMargins(float*, float*, float*, float*)
									const;
			void				SetItemMargins(float, float, float, float);

			bool				IsTracking() const;

			void				Install(BWindow* window);
			void				Uninstall();
			void				SetSuper(BMenu* menu);
			void				SetSuperItem(BMenuItem* item);
			void				InvokeItem(BMenuItem* item, bool now = false);
			void				QuitTracking(bool thisMenuOnly = true);
			bool				HasSubmenus() { return fMenu->fHasSubmenus; }

	static	status_t			CreateBitmaps();
	static	void				DeleteBitmaps();

	static	const BBitmap*		MenuItemShift();
	static	const BBitmap*		MenuItemControl();
	static	const BBitmap*		MenuItemOption();
	static	const BBitmap*		MenuItemCommand();
	static	const BBitmap*		MenuItemMenu();

private:
			BMenu*				fMenu;

	static	BBitmap*			sMenuItemShift;
	static	BBitmap*			sMenuItemControl;
	static	BBitmap*			sMenuItemOption;
	static	BBitmap*			sMenuItemAlt;
	static	BBitmap*			sMenuItemMenu;

};

struct MenuTrackState {
	thread_id trackThread;
	BLocker locker;
	bool quit;
	BMenu* rootMenu;
	BMenu* curMenu;
	BMenu* cursorMenu; // menu that holding pointer event mask
	BMenuItem* invokedItem;
	bool cursorInside, cursorObscured;
	BPoint clickPoint, enterPoint;
	BRect navAreaRectAbove;
	BRect navAreaRectBelow;
	bigtime_t selectedTime;
	bigtime_t navigationAreaTime;
	ObjectDeleter<BMessageRunner> navigationAreaTimer;
};

};	// namespace BPrivate


// Note: since sqrt is slow, we don't use it and return the square of the
// distance
#define square(x) ((x) * (x))
static inline float
point_distance(const BPoint &pointA, const BPoint &pointB)
{
	return square(pointA.x - pointB.x) + square(pointA.y - pointB.y);
}
#undef square


#endif	// __MENU_PRIVATE_H
