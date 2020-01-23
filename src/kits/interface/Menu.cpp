/*
 * Copyright 2001-2018 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *		Marc Flerackers, mflerackers@androme.be
 *		Rene Gollent, anevilyak@gmail.com
 *		John Scipione, jscipione@gmail.com
 */


#include <Menu.h>

#include <algorithm>
#include <new>
#include <set>

#include <ctype.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <SystemCatalog.h>
#include <UnicodeChar.h>
#include <Window.h>
#include <AutoLocker.h>

#include <AppServerLink.h>
#include <AutoDeleter.h>
#include <binary_compatibility/Interface.h>
#include <BMCPrivate.h>
#include <MenuPrivate.h>
#include <MenuWindow.h>
#include <ServerProtocol.h>

#include "utf8_functions.h"


#define USE_CACHED_MENUWINDOW 1

#define SHOW_NAVIGATION_AREA 0

using BPrivate::gSystemCatalog;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Menu"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "Menu")


using std::nothrow;
using BPrivate::BMenuWindow;

namespace BPrivate {

class TriggerList {
public:
	TriggerList() {}
	~TriggerList() {}

	bool HasTrigger(uint32 c)
		{ return fList.find(BUnicodeChar::ToLower(c)) != fList.end(); }

	bool AddTrigger(uint32 c)
	{
		fList.insert(BUnicodeChar::ToLower(c));
		return true;
	}

private:
	std::set<uint32> fList;
};


class ExtraMenuData {
public:
	menu_tracking_hook	trackingHook;
	void*				trackingState;

	// Used to track when the menu would be drawn offscreen and instead gets
	// shifted back on the screen towards the left. This information
	// allows us to draw submenus in the same direction as their parents.
	bool				frameShiftedLeft;

	ExtraMenuData()
	{
		trackingHook = NULL;
		trackingState = NULL;
		frameShiftedLeft = false;
	}
};


typedef int (*compare_func)(const BMenuItem*, const BMenuItem*);

struct MenuItemComparator
{
	MenuItemComparator(compare_func compareFunc)
		:
		fCompareFunc(compareFunc)
	{
	}

	bool operator () (const BMenuItem* item1, const BMenuItem* item2) {
		return fCompareFunc(item1, item2) < 0;
	}

private:
	compare_func fCompareFunc;
};


}	// namespace BPrivate


menu_info BMenu::sMenuInfo;

uint32 BMenu::sShiftKey;
uint32 BMenu::sControlKey;
uint32 BMenu::sOptionKey;
uint32 BMenu::sCommandKey;
uint32 BMenu::sMenuKey;

static property_info sPropList[] = {
	{ "Enabled", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if menu or menu item is "
		"enabled; false otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Enabled", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Enables or disables menu or menu item.",
		0, { B_BOOL_TYPE }
	},

	{ "Label", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns the string label of the menu or "
		"menu item.",
		0, { B_STRING_TYPE }
	},

	{ "Label", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Sets the string label of the menu or menu "
		"item.",
		0, { B_STRING_TYPE }
	},

	{ "Mark", { B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Returns true if the menu item or the "
		"menu's superitem is marked; false otherwise.",
		0, { B_BOOL_TYPE }
	},

	{ "Mark", { B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Marks or unmarks the menu item or the "
		"menu's superitem.",
		0, { B_BOOL_TYPE }
	},

	{ "Menu", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label "
		"found in \"data\" and the int32 command found in \"what\" (used as "
		"the what field in the BMessage sent by the item)." , 0, {},
		{ 	{{{"data", B_STRING_TYPE}}}
		}
	},

	{ "Menu", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the selected menu or menus.", 0, {}
	},

	{ "Menu", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the "
		"current specifier off the stack.", 0, {}
	},

	{ "MenuItem", { B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 }, "Counts the number of menu items in the "
		"specified menu.",
		0, { B_INT32_TYPE }
	},

	{ "MenuItem", { B_CREATE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Adds a new menu item at the specified index with the text label "
		"found in \"data\" and the int32 command found in \"what\" (used as "
		"the what field in the BMessage sent by the item).", 0, {},
		{	{ {{"data", B_STRING_TYPE },
			{"be:invoke_message", B_MESSAGE_TYPE},
			{"what", B_INT32_TYPE},
			{"be:target", B_MESSENGER_TYPE}} }
		}
	},

	{ "MenuItem", { B_DELETE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Removes the specified menu item from its parent menu."
	},

	{ "MenuItem", { B_EXECUTE_PROPERTY, 0 },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Invokes the specified menu item."
	},

	{ "MenuItem", { },
		{ B_NAME_SPECIFIER, B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, 0 },
		"Directs scripting message to the specified menu, first popping the "
		"current specifier off the stack."
	},

	{ 0 }
};


// note: this is redefined to localized one in BMenu::_InitData
const char* BPrivate::kEmptyMenuLabel = "<empty>";


struct BMenu::LayoutData {
	BSize	preferred;
	uint32	lastResizingMode;
};


// #pragma mark - BMenu


BMenu::BMenu(const char* name, menu_layout layout)
	:
	BView(BRect(0, 0, 0, 0), name, 0, B_WILL_DRAW),
	fTrackState(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


BMenu::BMenu(const char* name, float width, float height)
	:
	BView(BRect(0.0f, 0.0f, 0.0f, 0.0f), name, 0, B_WILL_DRAW),
	fTrackState(NULL),
	fPad(14.0f, 2.0f, 20.0f, 0.0f),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fLayout(B_ITEMS_IN_MATRIX),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


BMenu::BMenu(BMessage* archive)
	:
	BView(archive),
	fTrackState(NULL),
	fPad(14.0f, 2.0f, 20.0f, 0.0f),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fLayout(B_ITEMS_IN_ROW),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(true),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(archive);
}


BMenu::~BMenu()
{
	_DeleteMenuWindow();

	RemoveItems(0, CountItems(), true);

	delete fInitMatrixSize;
	delete fExtraMenuData;
	delete fLayoutData;
}


BArchivable*
BMenu::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BMenu"))
		return new (nothrow) BMenu(archive);

	return NULL;
}


status_t
BMenu::Archive(BMessage* data, bool deep) const
{
	status_t err = BView::Archive(data, deep);

	if (err == B_OK && Layout() != B_ITEMS_IN_ROW)
		err = data->AddInt32("_layout", Layout());
	if (err == B_OK)
		err = data->AddBool("_rsize_to_fit", fResizeToFit);
	if (err == B_OK)
		err = data->AddBool("_disable", !IsEnabled());
	if (err ==  B_OK)
		err = data->AddBool("_radio", IsRadioMode());
	if (err == B_OK)
		err = data->AddBool("_trig_disabled", AreTriggersEnabled());
	if (err == B_OK)
		err = data->AddBool("_dyn_label", fDynamicName);
	if (err == B_OK)
		err = data->AddFloat("_maxwidth", fMaxContentWidth);
	if (err == B_OK && deep) {
		BMenuItem* item = NULL;
		int32 index = 0;
		while ((item = ItemAt(index++)) != NULL) {
			BMessage itemData;
			item->Archive(&itemData, deep);
			err = data->AddMessage("_items", &itemData);
			if (err != B_OK)
				break;
			if (fLayout == B_ITEMS_IN_MATRIX) {
				err = data->AddRect("_i_frames", item->fBounds);
			}
		}
	}

	return err;
}


void
BMenu::AttachedToWindow()
{
	BView::AttachedToWindow();

	_GetShiftKey(sShiftKey);
	_GetControlKey(sControlKey);
	_GetCommandKey(sCommandKey);
	_GetOptionKey(sOptionKey);
	_GetMenuKey(sMenuKey);

	fAttachAborted = _AddDynamicItems();

	if (!fAttachAborted) {
		_CacheFontInfo();
		_LayoutItems(0);
		_UpdateWindowViewSize(false);
	}
}


void
BMenu::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BMenu::AllAttached()
{
	BView::AllAttached();
}


void
BMenu::AllDetached()
{
	BView::AllDetached();
}


void
BMenu::Draw(BRect updateRect)
{
	if (_RelayoutIfNeeded()) {
		Invalidate();
		return;
	}

	DrawBackground(updateRect);
	DrawItems(updateRect);

	#if SHOW_NAVIGATION_AREA
	if (fTrackState != NULL) {
		BRect above, below;
		{
			AutoLocker<BLocker> locker(fTrackState->locker);
			above = ConvertFromScreen(fTrackState->navAreaRectAbove);
			below = ConvertFromScreen(fTrackState->navAreaRectBelow);
		}
		bool isLeft = above.left == 0;
		PushState();
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(0xff, 0, 0, 0x66);
		if (!isLeft) {
			FillTriangle(above.LeftBottom(), above.RightTop(), above.RightBottom());
			FillTriangle(below.LeftTop(), below.RightBottom(), above.RightTop());
		} else {
			FillTriangle(above.RightBottom(), above.LeftTop(), above.LeftBottom());
			FillTriangle(below.RightTop(), below.LeftBottom(), above.LeftTop());
		}
		PopState();
	}
	#endif
}


const static bigtime_t kNavigationAreaTimeout = 1000000;

void
BMenu::MessageReceived(BMessage* message)
{
	if (message->HasSpecifiers())
		return _ScriptReceived(message);

	if (fTrackState == NULL)
		return BView::MessageReceived(message);

	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaY = 0;
			message->FindFloat("be:wheel_delta_y", &deltaY);
			if (deltaY == 0)
				return;

			BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
			if (window == NULL)
				return;

			float largeStep;
			float smallStep;
			window->GetSteps(&smallStep, &largeStep);

			// pressing the shift key scrolls faster
			if ((modifiers() & B_SHIFT_KEY) != 0)
				deltaY *= largeStep;
			else
				deltaY *= smallStep;

			window->TryScrollBy(deltaY);
			break;
		}

		case B_MOUSE_DOWN: {
			printf("B_MOUSE_DOWN, _IsStickyMode: %d\n", _IsStickyMode());
			BPoint where = B_ORIGIN;
			message->FindPoint("be:view_where", &where);
			{
				AutoLocker<BLocker> locker(fTrackState->locker);
				if (!fTrackState->cursorInside) {
					_QuitTracking(false);
					BView::MessageReceived(message);
					return;
				}
			}
			if (_IsStickyMode())
				_SetStickyMode(false);

			_CallTrackingHook();

			BView::MessageReceived(message);
			break;
		}

		case B_MOUSE_UP: {
			printf("B_MOUSE_UP, _IsStickyMode: %d\n", _IsStickyMode());
			BPoint where = B_ORIGIN;
			message->FindPoint("be:view_where", &where);
			BMenuItem* item = _HitTestItems(where, B_ORIGIN);
			if (
				!_IsStickyMode() || (
					!(fExtraRect != NULL && fExtraRect->Contains(where)) &&
					dynamic_cast<BMenuWindow*>(Window()) == NULL &&
					(item == NULL || item->Submenu() == NULL)
				)
			) {
				if (item != NULL) _InvokeItem(item);
				_QuitTracking(false);
			}
			_CallTrackingHook();
			BView::MessageReceived(message);
			break;
		}

		case B_MOUSE_MOVED: {
			BPoint where = B_ORIGIN;
			int32 buttons = 0;
			int32 transit = B_OUTSIDE_VIEW;

			message->FindPoint("be:view_where", &where);
			message->FindInt32("buttons", &buttons);
			message->FindInt32("be:transit", &transit);

			if (fTrackState == NULL) {
				BView::MessageReceived(message);
				return;
			}

			{
				AutoLocker<BLocker> locker(fTrackState->locker);

				BRect checkRect(-8, -8, 8, 8);
				checkRect.OffsetBy(fTrackState->clickPoint);

				if (_IsStickyMode() && !checkRect.Contains(ConvertToScreen(where))) {
					printf("outside of checkRect\n");
					_SetStickyMode(false);
				}

				switch (transit) {
					case B_ENTERED_VIEW:
						fTrackState->cursorMenu = this;
						fTrackState->cursorMenu->SetEventMask(B_POINTER_EVENTS, 0);
						fTrackState->cursorInside = true;
						break;
					case B_EXITED_VIEW:
						if (fTrackState->cursorMenu == this)
							fTrackState->cursorInside = false;

						if ((fSelected != NULL) && (fSelected->Submenu() == NULL))
							_SelectItem(NULL);

						break;
				}

				if (fTrackState->cursorMenu != this)
					SetEventMask(0, 0);
			}

			switch (transit) {
				case B_ENTERED_VIEW:
				case B_INSIDE_VIEW: {
					{
						AutoLocker<BLocker> locker(fTrackState->locker);
						if (fTrackState->cursorObscured) {
							if (transit == B_ENTERED_VIEW) {
								fTrackState->enterPoint = where;
								break;
							}
							if (fTrackState->enterPoint == where)
								break;

							fTrackState->cursorObscured = false;
						}
					}
					BMenuItem* oldSelected = fSelected;
					BMenuItem* item = _HitTestItems(where, B_ORIGIN);
					if (item == NULL) {
						if ((fSelected != NULL) && (fSelected->Submenu() == NULL))
							_SelectItem(NULL);
					} else if (fSelected != item) {
						if (!_HitNavigationArea(where, item)) {
							fTrackState->navigationAreaTimer.Unset();
							_SelectItem(item, true);
						} else {
							if (fTrackState->navigationAreaTimer.Get() != NULL)
								fTrackState->navigationAreaTimer.SetTo(new(std::nothrow) BMessageRunner(BMessenger(this), BMessage(navigationAreaTimeoutMsg), kNavigationAreaTimeout, 1));
						}
					}
					{
						AutoLocker<BLocker> locker(fTrackState->locker);
						if (oldSelected != fSelected)
							fTrackState->curMenu = this;
					}
					break;
				}
			}

			_CallTrackingHook();

			BView::MessageReceived(message);
			break;
		}

		case navigationAreaTimeoutMsg: {
			if (fTrackState->navigationAreaTimer.Get() != NULL) {
				fTrackState->navigationAreaTimer.Unset();
				BPoint where;
				GetMouse(&where, NULL);
				BMenuItem* oldSelected = fSelected;
				BMenuItem* item = _HitTestItems(where, B_ORIGIN);
				if (item == NULL) {
					if ((fSelected != NULL) && (fSelected->Submenu() == NULL))
						_SelectItem(NULL);
				} else
					_SelectItem(item, true);

				{
					AutoLocker<BLocker> locker(fTrackState->locker);
					if (oldSelected != fSelected)
						fTrackState->curMenu = this;
				}
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BMenu::KeyDown(const char* bytes, int32 numBytes)
{
	if (fTrackState == NULL)
		return;

	AutoLocker<BLocker> locker(fTrackState->locker);

	if (fTrackState->curMenu != this) {
		BMessenger messenger(fTrackState->curMenu);
		messenger.SendMessage(Window()->CurrentMessage());
		return;
	}

	fTrackState->cursorObscured = true;
	be_app->ObscureCursor();

	switch (bytes[0]) {
		case B_UP_ARROW:
			if (fLayout == B_ITEMS_IN_COLUMN) {
				_SelectNextItem(fSelected, false);
			} else if (fLayout == B_ITEMS_IN_ROW || fLayout == B_ITEMS_IN_MATRIX) {
				_QuitTracking(true);
			}
			break;

		case B_DOWN_ARROW:
			if (fLayout == B_ITEMS_IN_COLUMN) {
				_SelectNextItem(fSelected, true);
			} else if (fLayout == B_ITEMS_IN_ROW || fLayout == B_ITEMS_IN_MATRIX) {
				if (fSelected != NULL) {
					BMenu* subMenu = fSelected->Submenu();
					if (subMenu != NULL && subMenu->LockLooper()) {
						subMenu->_SelectNextItem(subMenu->fSelected, true);
						subMenu->UnlockLooper();
						fTrackState->curMenu = subMenu;
					}
				}
			}
			break;

		case B_LEFT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW || fLayout == B_ITEMS_IN_MATRIX) {
				_SelectNextItem(fSelected, false);
			} else if (fLayout == B_ITEMS_IN_COLUMN) {
				_QuitTracking(true);
			}
			break;

		case B_RIGHT_ARROW:
			if (fLayout == B_ITEMS_IN_ROW || fLayout == B_ITEMS_IN_MATRIX) {
				_SelectNextItem(fSelected, true);
			} else if (fLayout == B_ITEMS_IN_COLUMN) {
				if (fSelected != NULL) {
					BMenu* subMenu = fSelected->Submenu();
					if (subMenu != NULL && subMenu->LockLooper()) {
						subMenu->_SelectNextItem(subMenu->fSelected, true);
						subMenu->UnlockLooper();
						fTrackState->curMenu = subMenu;
					}
				}
			}
			break;

		case B_PAGE_UP:
		case B_PAGE_DOWN:
		{
			BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
			if (window == NULL || !window->HasScrollers())
				break;

			int32 deltaY = bytes[0] == B_PAGE_UP ? -1 : 1;

			float largeStep;
			window->GetSteps(NULL, &largeStep);
			window->TryScrollBy(deltaY * largeStep);
			break;
		}

		case B_ENTER:
		case B_SPACE:
			if (fSelected != NULL) {
				_InvokeItem(fSelected);
				_QuitTracking(false);
			}
			break;

		case B_ESCAPE:
			_QuitTracking(false);
			break;

		default:
		{
			uint32 trigger = BUnicodeChar::FromUTF8(&bytes);

			for (uint32 i = CountItems(); i-- > 0;) {
				BMenuItem* item = ItemAt(i);
				if (item->fTriggerIndex < 0 || item->fTrigger != trigger)
					continue;

				if (item->Submenu()) {
					_SelectItem(item, true, false);
					BMenu* subMenu = fSelected->Submenu();
					if (subMenu != NULL && subMenu->LockLooper()) {
						subMenu->_SelectNextItem(subMenu->fSelected, true);
						subMenu->UnlockLooper();
						fTrackState->curMenu = subMenu;
					}
				} else {
					_InvokeItem(item);
					_QuitTracking(false);
				}
				break;
			}
			break;
		}
	}
}


BSize
BMenu::MinSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->MinSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


BSize
BMenu::MaxSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->MaxSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


BSize
BMenu::PreferredSize()
{
	_ValidatePreferredSize();

	BSize size = (GetLayout() != NULL ? GetLayout()->PreferredSize()
		: fLayoutData->preferred);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


void
BMenu::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fLayoutData->preferred.width;

	if (_height)
		*_height = fLayoutData->preferred.height;
}


void
BMenu::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


void
BMenu::DoLayout()
{
	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != NULL) {
		BView::DoLayout();
		return;
	}

	if (_RelayoutIfNeeded())
		Invalidate();
}


void
BMenu::FrameMoved(BPoint where)
{
	BView::FrameMoved(where);
}


void
BMenu::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
}


void
BMenu::InvalidateLayout()
{
	fUseCachedMenuLayout = false;
	// This method exits for backwards compatibility reasons, it is used to
	// invalidate the menu layout, but we also use call
	// BView::InvalidateLayout() for good measure. Don't delete this method!
	BView::InvalidateLayout(false);
}


void
BMenu::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
}


bool
BMenu::AddItem(BMenuItem* item)
{
	return AddItem(item, CountItems());
}


bool
BMenu::AddItem(BMenuItem* item, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, int32) this method can only "
			"be called if the menu layout is not B_ITEMS_IN_MATRIX");
	}

	if (item == NULL)
		return false;

	const bool locked = LockLooper();

	if (!_AddItem(item, index)) {
		if (locked)
			UnlockLooper();
		return false;
	}

	InvalidateLayout();
	if (locked) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
		UnlockLooper();
	}

	return true;
}


bool
BMenu::AddItem(BMenuItem* item, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");
	}

	if (item == NULL)
		return false;

	const bool locked = LockLooper();

	item->fBounds = frame;

	int32 index = CountItems();
	if (!_AddItem(item, index)) {
		if (locked)
			UnlockLooper();
		return false;
	}

	if (locked) {
		if (!Window()->IsHidden()) {
			_LayoutItems(index);
			Invalidate();
		}
		UnlockLooper();
	}

	return true;
}


bool
BMenu::AddItem(BMenu* submenu)
{
	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, CountItems())) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddItem(BMenu* submenu, int32 index)
{
	if (fLayout == B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenuItem*, int32) this method can only "
			"be called if the menu layout is not B_ITEMS_IN_MATRIX");
	}

	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, index)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddItem(BMenu* submenu, BRect frame)
{
	if (fLayout != B_ITEMS_IN_MATRIX) {
		debugger("BMenu::AddItem(BMenu*, BRect) this method can only "
			"be called if the menu layout is B_ITEMS_IN_MATRIX");
	}

	BMenuItem* item = new (nothrow) BMenuItem(submenu);
	if (item == NULL)
		return false;

	if (!AddItem(item, frame)) {
		item->fSubmenu = NULL;
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::AddList(BList* list, int32 index)
{
	// TODO: test this function, it's not documented in the bebook.
	if (list == NULL)
		return false;

	bool locked = LockLooper();

	int32 numItems = list->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem* item = static_cast<BMenuItem*>(list->ItemAt(i));
		if (item != NULL) {
			if (!_AddItem(item, index + i))
				break;
		}
	}

	InvalidateLayout();
	if (locked && Window() != NULL && !Window()->IsHidden()) {
		// Make sure we update the layout if needed.
		_LayoutItems(index);
		_UpdateWindowViewSize(false);
		Invalidate();
	}

	if (locked)
		UnlockLooper();

	return true;
}


bool
BMenu::AddSeparatorItem()
{
	BMenuItem* item = new (nothrow) BSeparatorItem();
	if (!item || !AddItem(item, CountItems())) {
		delete item;
		return false;
	}

	return true;
}


bool
BMenu::RemoveItem(BMenuItem* item)
{
	return _RemoveItems(0, 0, item, false);
}


BMenuItem*
BMenu::RemoveItem(int32 index)
{
	BMenuItem* item = ItemAt(index);
	if (item != NULL)
		_RemoveItems(index, 1, NULL, false);
	return item;
}


bool
BMenu::RemoveItems(int32 index, int32 count, bool deleteItems)
{
	return _RemoveItems(index, count, NULL, deleteItems);
}


bool
BMenu::RemoveItem(BMenu* submenu)
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (static_cast<BMenuItem*>(fItems.ItemAtFast(i))->Submenu()
				== submenu) {
			return _RemoveItems(i, 1, NULL, false);
		}
	}

	return false;
}


int32
BMenu::CountItems() const
{
	return fItems.CountItems();
}


BMenuItem*
BMenu::ItemAt(int32 index) const
{
	return static_cast<BMenuItem*>(fItems.ItemAt(index));
}


BMenu*
BMenu::SubmenuAt(int32 index) const
{
	BMenuItem* item = static_cast<BMenuItem*>(fItems.ItemAt(index));
	return item != NULL ? item->Submenu() : NULL;
}


int32
BMenu::IndexOf(BMenuItem* item) const
{
	return fItems.IndexOf(item);
}


int32
BMenu::IndexOf(BMenu* submenu) const
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		if (ItemAt(i)->Submenu() == submenu)
			return i;
	}

	return -1;
}


BMenuItem*
BMenu::FindItem(const char* label) const
{
	BMenuItem* item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Label() && strcmp(item->Label(), label) == 0)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(label);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


BMenuItem*
BMenu::FindItem(uint32 command) const
{
	BMenuItem* item = NULL;

	for (int32 i = 0; i < CountItems(); i++) {
		item = ItemAt(i);

		if (item->Command() == command)
			return item;

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(command);
			if (item != NULL)
				return item;
		}
	}

	return NULL;
}


status_t
BMenu::SetTargetForItems(BHandler* handler)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(handler);
		if (status < B_OK)
			break;
	}

	return status;
}


status_t
BMenu::SetTargetForItems(BMessenger messenger)
{
	status_t status = B_OK;
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		status = ItemAt(i)->SetTarget(messenger);
		if (status < B_OK)
			break;
	}

	return status;
}


void
BMenu::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (dynamic_cast<_BMCMenuBar_*>(Supermenu()) != NULL)
		Supermenu()->SetEnabled(enable);

	if (fSuperitem)
		fSuperitem->SetEnabled(enable);
}


void
BMenu::SetRadioMode(bool on)
{
	fRadioMode = on;
	if (!on)
		SetLabelFromMarked(false);
}


void
BMenu::SetTriggersEnabled(bool enable)
{
	fTriggerEnabled = enable;
}


void
BMenu::SetMaxContentWidth(float width)
{
	fMaxContentWidth = width;
}


void
BMenu::SetLabelFromMarked(bool on)
{
	fDynamicName = on;
	if (on)
		SetRadioMode(true);
}


bool
BMenu::IsLabelFromMarked()
{
	return fDynamicName;
}


bool
BMenu::IsEnabled() const
{
	if (!fEnabled)
		return false;

	return fSuper ? fSuper->IsEnabled() : true ;
}


bool
BMenu::IsRadioMode() const
{
	return fRadioMode;
}


bool
BMenu::AreTriggersEnabled() const
{
	return fTriggerEnabled;
}


bool
BMenu::IsRedrawAfterSticky() const
{
	return false;
}


float
BMenu::MaxContentWidth() const
{
	return fMaxContentWidth;
}


BMenuItem*
BMenu::FindMarked()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return item;
	}

	return NULL;
}


int32
BMenu::FindMarkedIndex()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return i;
	}

	return -1;
}


BMenu*
BMenu::Supermenu() const
{
	return fSuper;
}


BMenuItem*
BMenu::Superitem() const
{
	return fSuperitem;
}


BHandler*
BMenu::ResolveSpecifier(BMessage* msg, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	BPropertyInfo propInfo(sPropList);
	BHandler* target = NULL;

	if (propInfo.FindMatch(msg, index, specifier, form, property) >= B_OK) {
		target = this;
	}

	if (!target)
		target = BView::ResolveSpecifier(msg, index, specifier, form,
		property);

	return target;
}


status_t
BMenu::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-menu");

	if (err < B_OK)
		return err;

	BPropertyInfo propertyInfo(sPropList);
	err = data->AddFlat("messages", &propertyInfo);

	if (err < B_OK)
		return err;

	return BView::GetSupportedSuites(data);
}


status_t
BMenu::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BMenu::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BMenu::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BMenu::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BMenu::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BMenu::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BMenu::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BMenu::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BMenu::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BMenu::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


// #pragma mark - BMenu protected methods


BMenu::BMenu(BRect frame, const char* name, uint32 resizingMode, uint32 flags,
	menu_layout layout, bool resizeToFit)
	:
	BView(frame, name, resizingMode, flags),
	fTrackState(NULL),
	fSelected(NULL),
	fCachedMenuWindow(NULL),
	fSuper(NULL),
	fSuperitem(NULL),
	fAscent(-1.0f),
	fDescent(-1.0f),
	fFontHeight(-1.0f),
	fLayout(layout),
	fExtraRect(NULL),
	fMaxContentWidth(0.0f),
	fInitMatrixSize(NULL),
	fExtraMenuData(NULL),
	fTrigger(0),
	fResizeToFit(resizeToFit),
	fUseCachedMenuLayout(false),
	fEnabled(true),
	fDynamicName(false),
	fRadioMode(false),
	fTrackNewBounds(false),
	fStickyMode(false),
	fIgnoreHidden(true),
	fTriggerEnabled(true),
	fHasSubmenus(false),
	fAttachAborted(false)
{
	_InitData(NULL);
}


void
BMenu::SetItemMargins(float left, float top, float right, float bottom)
{
	fPad.Set(left, top, right, bottom);
}


void
BMenu::GetItemMargins(float* _left, float* _top, float* _right,
	float* _bottom) const
{
	if (_left != NULL)
		*_left = fPad.left;

	if (_top != NULL)
		*_top = fPad.top;

	if (_right != NULL)
		*_right = fPad.right;

	if (_bottom != NULL)
		*_bottom = fPad.bottom;
}


menu_layout
BMenu::Layout() const
{
	return fLayout;
}


void
BMenu::Show()
{
	Show(false);
}


void
BMenu::Show(bool selectFirst)
{
	_Install(NULL);
	_Show(selectFirst);
}


void
BMenu::Hide()
{
	_Hide();
	_Uninstall();
}


BMenuItem*
BMenu::Track(bool sticky, BRect* clickToOpenRect)
{
	if (sticky && LockLooper()) {
		//RedrawAfterSticky(Bounds());
			// the call above didn't do anything, so I've removed it for now
		UnlockLooper();
	}

	if (clickToOpenRect != NULL && LockLooper()) {
		fExtraRect = clickToOpenRect;
		ConvertFromScreen(fExtraRect);
		UnlockLooper();
	}

	_SetStickyMode(sticky);

	int32 action;
	BMenuItem* menuItem = _Track(&action);

	fExtraRect = NULL;

	return menuItem;
}


// #pragma mark - BMenu private methods


bool
BMenu::AddDynamicItem(add_state state)
{
	// Implemented in subclasses
	return false;
}


void
BMenu::DrawBackground(BRect updateRect)
{
	rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;

	if (IsFocus())
		flags |= BControlLook::B_FOCUSED;

	BRect rect = Bounds();
	uint32 borders = BControlLook::B_LEFT_BORDER
		| BControlLook::B_RIGHT_BORDER;
	if (Window() != NULL && Parent() != NULL) {
		if (Parent()->Frame().top == Window()->Bounds().top)
			borders |= BControlLook::B_TOP_BORDER;

		if (Parent()->Frame().bottom == Window()->Bounds().bottom)
			borders |= BControlLook::B_BOTTOM_BORDER;
	} else {
		borders |= BControlLook::B_TOP_BORDER
			| BControlLook::B_BOTTOM_BORDER;
	}
	be_control_look->DrawMenuBackground(this, rect, updateRect, base, flags,
		borders);
}


void
BMenu::SetTrackingHook(menu_tracking_hook func, void* state)
{
	fExtraMenuData->trackingHook = func;
	fExtraMenuData->trackingState = state;
}


// #pragma mark - Reorder item methods


void
BMenu::SortItems(int (*compare)(const BMenuItem*, const BMenuItem*))
{
	BMenuItem** begin = (BMenuItem**)fItems.Items();
	BMenuItem** end = begin + fItems.CountItems();

	std::stable_sort(begin, end, BPrivate::MenuItemComparator(compare));

	InvalidateLayout();
	if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
		_LayoutItems(0);
		Invalidate();
		UnlockLooper();
	}
}


bool
BMenu::SwapItems(int32 indexA, int32 indexB)
{
	bool swapped = fItems.SwapItems(indexA, indexB);
	if (swapped) {
		InvalidateLayout();
		if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
			_LayoutItems(std::min(indexA, indexB));
			Invalidate();
			UnlockLooper();
		}
	}

	return swapped;
}


bool
BMenu::MoveItem(int32 indexFrom, int32 indexTo)
{
	bool moved = fItems.MoveItem(indexFrom, indexTo);
	if (moved) {
		InvalidateLayout();
		if (Window() != NULL && !Window()->IsHidden() && LockLooper()) {
			_LayoutItems(std::min(indexFrom, indexTo));
			Invalidate();
			UnlockLooper();
		}
	}

	return moved;
}


void BMenu::_ReservedMenu3() {}
void BMenu::_ReservedMenu4() {}
void BMenu::_ReservedMenu5() {}
void BMenu::_ReservedMenu6() {}


void
BMenu::_InitData(BMessage* archive)
{
	BPrivate::kEmptyMenuLabel = B_TRANSLATE("<empty>");

	// TODO: Get _color, _fname, _fflt from the message, if present
	BFont font;
	font.SetFamilyAndStyle(sMenuInfo.f_family, sMenuInfo.f_style);
	font.SetSize(sMenuInfo.font_size);
	SetFont(&font, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);

	fExtraMenuData = new (nothrow) BPrivate::ExtraMenuData();

	const float labelSpacing = be_control_look->DefaultLabelSpacing();
	fPad = BRect(ceilf(labelSpacing * 2.3f), ceilf(labelSpacing / 3.0f),
		ceilf((labelSpacing / 3.0f) * 10.0f), 0.0f);

	fLayoutData = new LayoutData;
	fLayoutData->lastResizingMode = ResizingMode();

	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetViewColor(B_TRANSPARENT_COLOR);

	fTriggerEnabled = sMenuInfo.triggers_always_shown;

	if (archive != NULL) {
		archive->FindInt32("_layout", (int32*)&fLayout);
		archive->FindBool("_rsize_to_fit", &fResizeToFit);
		bool disabled;
		if (archive->FindBool("_disable", &disabled) == B_OK)
			fEnabled = !disabled;
		archive->FindBool("_radio", &fRadioMode);

		bool disableTrigger = false;
		archive->FindBool("_trig_disabled", &disableTrigger);
		fTriggerEnabled = !disableTrigger;

		archive->FindBool("_dyn_label", &fDynamicName);
		archive->FindFloat("_maxwidth", &fMaxContentWidth);

		BMessage msg;
		for (int32 i = 0; archive->FindMessage("_items", i, &msg) == B_OK; i++) {
			BArchivable* object = instantiate_object(&msg);
			if (BMenuItem* item = dynamic_cast<BMenuItem*>(object)) {
				BRect bounds;
				if (fLayout == B_ITEMS_IN_MATRIX
					&& archive->FindRect("_i_frames", i, &bounds) == B_OK)
					AddItem(item, bounds);
				else
					AddItem(item);
			}
		}
	}
}


bool
BMenu::_Show(bool selectFirstItem)
{
	if (Window() != NULL)
		return false;

	// See if the supermenu has a cached menuwindow,
	// and use that one if possible.
	BMenuWindow* window = NULL;
	bool ourWindow = false;
	if (fSuper != NULL) {
		fSuperbounds = fSuper->ConvertToScreen(fSuper->Bounds());
		window = fSuper->_MenuWindow();
	}

	// Otherwise, create a new one
	// This happens for "stand alone" BPopUpMenus
	// (i.e. not within a BMenuField)
	if (window == NULL) {
		// Menu windows get the BMenu's handler name
		window = new (nothrow) BMenuWindow(Name());
		ourWindow = true;
	}

	if (window == NULL)
		return false;

	if (window->Lock()) {
		fAttachAborted = false;

		window->AttachMenu(this);

		if (ItemAt(0) != NULL) {
			float width, height;
			ItemAt(0)->GetContentSize(&width, &height);

			window->SetSmallStep(ceilf(height));
		}

		// Menu didn't have the time to add its items: aborting...
		if (fAttachAborted) {
			printf("fAttachAborted\n");
			window->DetachMenu();
			// TODO: Probably not needed, we can just let _hide() quit the
			// window.
			if (ourWindow)
				window->Quit();
			else
				window->Unlock();
			return false;
		}

		_UpdateWindowViewSize(true);
		window->Show();

		if (Supermenu() != NULL) {
			fTrackState = Supermenu()->fTrackState;
			fTriggerEnabled = Supermenu()->fTriggerEnabled;
		}

		if (selectFirstItem)
			_SelectItem(ItemAt(0), false);

		window->Unlock();
	}

	return true;
}


void
BMenu::_Hide()
{
	BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
	if (window == NULL || !window->Lock())
		return;

	if (fSelected != NULL)
		_SelectItem(NULL);

	BMenu *rootMenu = NULL;
	if (fTrackState != NULL) {
		AutoLocker<BLocker> locker(fTrackState->locker);
		if (fTrackState->cursorMenu == this) {
			SetEventMask(0, 0);
			rootMenu = fTrackState->rootMenu;
			fTrackState->cursorMenu = rootMenu;
		}
	}
	if (rootMenu != NULL && rootMenu->LockLooper()) {
		rootMenu->SetEventMask(B_POINTER_EVENTS, 0);
		rootMenu->UnlockLooper();
	}

	fTrackState = NULL;

	window->Hide();
	window->DetachMenu();
		// we don't want to be deleted when the window is removed

#if USE_CACHED_MENUWINDOW
	if (fSuper != NULL)
		window->Unlock();
	else
#endif
		window->Quit();
			// it's our window, quit it

	_DeleteMenuWindow();
		// Delete the menu window used by our submenus
}


void BMenu::_ScriptReceived(BMessage* message)
{
	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BView::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sPropList);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 0: // Enabled: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", IsEnabled());
			break;
		case 1: // Enabled: SET
			if (message->what == B_SET_PROPERTY) {
				bool isEnabled;
				err = message->FindBool("data", &isEnabled);
				if (err >= B_OK)
					SetEnabled(isEnabled);
			}
			break;
		case 2: // Label: GET
		case 3: // Label: SET
		case 4: // Mark: GET
		case 5: { // Mark: SET
			BMenuItem *item = Superitem();
			if (item != NULL)
				return Supermenu()->_ItemScriptReceived(message, item);

			break;
		}
		case 6: // Menu: CREATE
			if (message->what == B_CREATE_PROPERTY) {
				const char *label;
				ObjectDeleter<BMessage> invokeMessage(new BMessage());
				BMessenger target;
				ObjectDeleter<BMenuItem> item;
				err = message->FindString("data", &label);
				if (err >= B_OK) {
					invokeMessage.SetTo(new BMessage());
					err = message->FindInt32("what",
						(int32*)&invokeMessage->what);
					if (err == B_NAME_NOT_FOUND) {
						invokeMessage.Unset();
						err = B_OK;
					}
				}
				if (err >= B_OK) {
					item.SetTo(new BMenuItem(new BMenu(label),
						invokeMessage.Detach()));
				}
				if (err >= B_OK) {
					err = _InsertItemAtSpecifier(specifier, what, item.Get());
				}
				if (err >= B_OK)
					item.Detach();
			}
			break;
		case 7: { // Menu: DELETE
			if (message->what == B_DELETE_PROPERTY) {
				BMenuItem *item = NULL;
				int32 index;
				err = _ResolveItemSpecifier(specifier, what, item, &index);
				if (err >= B_OK) {
					if (item->Submenu() == NULL)
						err = B_BAD_VALUE;
					else {
						if (index >= 0)
							RemoveItem(index);
						else
							RemoveItem(item);
						delete item;
					}
				}
			}
			break;
		}
		case 8: { // Menu: *
			// TODO: check that submenu looper is running and handle it
			// correctly
			BMenu *submenu = NULL;
			BMenuItem *item;
			err = _ResolveItemSpecifier(specifier, what, item);
			if (err >= B_OK)
				submenu = item->Submenu();
			if (submenu != NULL) {
				message->PopSpecifier();
				return submenu->_ScriptReceived(message);
			}
			break;
		}
		case 9: // MenuItem: COUNT
			if (message->what == B_COUNT_PROPERTIES)
				err = replyMsg.AddInt32("result", CountItems());
			break;
		case 10: // MenuItem: CREATE
			if (message->what == B_CREATE_PROPERTY) {
				const char *label;
				ObjectDeleter<BMessage> invokeMessage(new BMessage());
				bool targetPresent = true;
				BMessenger target;
				ObjectDeleter<BMenuItem> item;
				err = message->FindString("data", &label);
				if (err >= B_OK) {
					err = message->FindMessage("be:invoke_message",
						invokeMessage.Get());
					if (err == B_NAME_NOT_FOUND) {
						err = message->FindInt32("what",
							(int32*)&invokeMessage->what);
						if (err == B_NAME_NOT_FOUND) {
							invokeMessage.Unset();
							err = B_OK;
						}
					}
				}
				if (err >= B_OK) {
					err = message->FindMessenger("be:target", &target);
					if (err == B_NAME_NOT_FOUND) {
						targetPresent = false;
						err = B_OK;
					}
				}
				if (err >= B_OK) {
					item.SetTo(new BMenuItem(label, invokeMessage.Detach()));
					if (targetPresent)
						err = item->SetTarget(target);
				}
				if (err >= B_OK) {
					err = _InsertItemAtSpecifier(specifier, what, item.Get());
				}
				if (err >= B_OK)
					item.Detach();
			}
			break;
		case 11: // MenuItem: DELETE
			if (message->what == B_DELETE_PROPERTY) {
				BMenuItem *item = NULL;
				int32 index;
				err = _ResolveItemSpecifier(specifier, what, item, &index);
				if (err >= B_OK) {
					if (index >= 0)
						RemoveItem(index);
					else
						RemoveItem(item);
					delete item;
				}
			}
			break;
		case 12: { // MenuItem: EXECUTE
			if (message->what == B_EXECUTE_PROPERTY) {
				BMenuItem *item = NULL;
				err = _ResolveItemSpecifier(specifier, what, item);
				if (err >= B_OK) {
					if (!item->IsEnabled())
						err = B_NOT_ALLOWED;
					else
						err = item->Invoke();
				}
			}
			break;
		}
		case 13: { // MenuItem: *
			BMenuItem *item = NULL;
			err = _ResolveItemSpecifier(specifier, what, item);
			if (err >= B_OK) {
				message->PopSpecifier();
				return _ItemScriptReceived(message, item);
			}
			break;
		}
		default:
			return BView::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
}


void BMenu::_ItemScriptReceived(BMessage* message, BMenuItem* item)
{
	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BView::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sPropList);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 0: // Enabled: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", item->IsEnabled());
			break;
		case 1: // Enabled: SET
			if (message->what == B_SET_PROPERTY) {
				bool isEnabled;
				err = message->FindBool("data", &isEnabled);
				if (err >= B_OK)
					item->SetEnabled(isEnabled);
			}
			break;
		case 2: // Label: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddString("result", item->Label());
			break;
		case 3: // Label: SET
			if (message->what == B_SET_PROPERTY) {
				const char *label;
				err = message->FindString("data", &label);
				if (err >= B_OK)
					item->SetLabel(label);
			}
		case 4: // Mark: GET
			if (message->what == B_GET_PROPERTY)
				err = replyMsg.AddBool("result", item->IsMarked());
			break;
		case 5: // Mark: SET
			if (message->what == B_SET_PROPERTY) {
				bool isMarked;
				err = message->FindBool("data", &isMarked);
				if (err >= B_OK)
					item->SetMarked(isMarked);
			}
			break;
		case 6: // Menu: CREATE
		case 7: // Menu: DELETE
		case 8: // Menu: *
		case 9: // MenuItem: COUNT
		case 10: // MenuItem: CREATE
		case 11: // MenuItem: DELETE
		case 12: // MenuItem: EXECUTE
		case 13: // MenuItem: *
			break;
		default:
			return BView::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;
		replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
}


status_t BMenu::_ResolveItemSpecifier(const BMessage& specifier, int32 what,
	BMenuItem*& item, int32 *_index)
{
	status_t err;
	item = NULL;
	int32 index = -1;
	switch (what) {
		case B_INDEX_SPECIFIER:
		case B_REVERSE_INDEX_SPECIFIER: {
			err = specifier.FindInt32("index", &index);
			if (err < B_OK)
				return err;
			if (what == B_REVERSE_INDEX_SPECIFIER)
				index = CountItems() - index;
			item = ItemAt(index);
			break;
		}
		case B_NAME_SPECIFIER: {
			const char* name;
			err = specifier.FindString("name", &name);
			if (err < B_OK)
				return err;
			item = FindItem(name);
			break;
		}
	}
	if (item == NULL)
		return B_BAD_INDEX;

	if (_index != NULL)
		*_index = index;

	return B_OK;
}


status_t BMenu::_InsertItemAtSpecifier(const BMessage& specifier, int32 what,
	BMenuItem* item)
{
	status_t err;
	switch (what) {
		case B_INDEX_SPECIFIER:
		case B_REVERSE_INDEX_SPECIFIER: {
			int32 index;
			err = specifier.FindInt32("index", &index);
			if (err < B_OK) return err;
			if (what == B_REVERSE_INDEX_SPECIFIER)
				index = CountItems() - index;
			if (!AddItem(item, index))
				return B_BAD_INDEX;
			break;
		}
		case B_NAME_SPECIFIER:
			return B_NOT_SUPPORTED;
			break;
	}

	return B_OK;
}


// #pragma mark - mouse tracking


BMenuItem*
BMenu::_Track(int32* action, int32 start)
{
	if (fTrackState != NULL) {
		printf("Track: already entered\n");
		return NULL;
	}
	printf("+Track\n");
	printf("sticky: %d\n", fStickyMode);
	if (fExtraRect != NULL) {
		printf("extraRect: "); fExtraRect->PrintToStream();
	}
	if (sMenuInfo.click_to_open)
		_SetStickyMode(true);
	BPrivate::MenuTrackState trackState;
	BMenuItem* item = NULL;
	BMenuItem* startItem = ItemAt(start);
	bool oldTriggerEnabled = fTriggerEnabled;
	thread_id senderThread;
	bool run = true;

	fTrackState = &trackState;
	fTrackState->trackThread = find_thread(NULL);
	fTrackState->quit = false;
	fTrackState->rootMenu = this;
	fTrackState->curMenu = this;
	fTrackState->cursorMenu = this;
	fTrackState->invokedItem = NULL;
	fTrackState->cursorInside = false;
	fTrackState->cursorObscured = false;
	fTrackState->navAreaRectAbove = BRect();
	fTrackState->navAreaRectBelow = BRect();
	//fTrackState->navigationAreaTimer = NULL;

	if (LockLooper()) {
		SetEventMask(B_POINTER_EVENTS, 0);
		BPoint where;
		uint32 btns;
		GetMouse(&where, &btns);
		fTrackState->cursorInside = Bounds().Contains(where);
		fTrackState->clickPoint = ConvertToScreen(where);
		if (startItem == NULL) {
			startItem = _HitTestItems(where, B_ORIGIN);
		}
		if (!oldTriggerEnabled && btns == 0) {
			fTrackState->cursorObscured = true;
			be_app->ObscureCursor();
			fTriggerEnabled = true;
			Invalidate();
		}
		_SelectItem(startItem, true, false);
		UnlockLooper();
	}
	while (run) {
		int32 cmd = receive_data(&senderThread, NULL, 0);
		switch (cmd) {
			case MENU_TRACK_CMD_DONE:
				run = false;
				break;
		}
	}

	item = fTrackState->invokedItem;

	if (LockLooper()) {
		// hide submenus
		_SelectItem(NULL);
		SetEventMask(0, 0);
		if (fTriggerEnabled != oldTriggerEnabled) {
			fTriggerEnabled = oldTriggerEnabled;
			Invalidate();
		}
/*
		if (fTrackState->navigationAreaTimer != NULL) {
			delete fTrackState->navigationAreaTimer;
			fTrackState->navigationAreaTimer = NULL;
		}
*/
		fTrackState = NULL;
		UnlockLooper();
	}

	// delete the menu window recycled for all the child menus
	_DeleteMenuWindow();

	printf("-Track\n");
	return item;
}


void
BMenu::_UpdateNavigationArea(BPoint position, BRect& navAreaRectAbove,
	BRect& navAreaRectBelow)
{
#define NAV_AREA_THRESHOLD    8

	// The navigation area is a region in which mouse-overs won't select
	// the item under the cursor. This makes it easier to navigate to
	// submenus, as the cursor can be moved to submenu items directly instead
	// of having to move it horizontally into the submenu first. The concept
	// is illustrated below:
	//
	// +-------+----+---------+
	// |       |   /|         |
	// |       |  /*|         |
	// |[2]--> | /**|         |
	// |       |/[4]|         |
	// |------------|         |
	// |    [1]     |   [6]   |
	// |------------|         |
	// |       |\[5]|         |
	// |[3]--> | \**|         |
	// |       |  \*|         |
	// |       |   \|         |
	// |       +----|---------+
	// |            |
	// +------------+
	//
	// [1] Selected item, cursor position ('position')
	// [2] Upper navigation area rectangle ('navAreaRectAbove')
	// [3] Lower navigation area rectangle ('navAreaRectBelow')
	// [4] Upper navigation area
	// [5] Lower navigation area
	// [6] Submenu
	//
	// The rectangles are used to calculate if the cursor is in the actual
	// navigation area (see _UpdateStateOpenSelect()).

	if (fSelected == NULL)
		return;

	BView* submenu = fSelected->Submenu()->Parent();

	if (submenu != NULL) {
		BRect menuBounds = ConvertToScreen(Bounds());

		BRect submenuBounds;
		if (submenu->LockLooper()) {
			submenuBounds = submenu->ConvertToScreen(submenu->Bounds());
			submenu->UnlockLooper();
		}

		if (menuBounds.left < submenuBounds.left) {
			navAreaRectAbove.Set(position.x + NAV_AREA_THRESHOLD,
				submenuBounds.top, menuBounds.right,
				position.y);
			navAreaRectBelow.Set(position.x + NAV_AREA_THRESHOLD,
				position.y, menuBounds.right,
				submenuBounds.bottom);
		} else {
			navAreaRectAbove.Set(menuBounds.left,
				submenuBounds.top, position.x - NAV_AREA_THRESHOLD,
				position.y);
			navAreaRectBelow.Set(menuBounds.left,
				position.y, position.x - NAV_AREA_THRESHOLD,
				submenuBounds.bottom);
		}
	} else {
		navAreaRectAbove = BRect();
		navAreaRectBelow = BRect();
	}

	#if SHOW_NAVIGATION_AREA
	Invalidate();
	#endif
}


bool
BMenu::_HitNavigationArea(BPoint position, BMenuItem* item)
{
	if (fLayout != B_ITEMS_IN_COLUMN)
		return false;

	BRect navAreaRectAbove, navAreaRectBelow;
	{
		AutoLocker<BLocker> locker(fTrackState->locker);
		navAreaRectAbove = fTrackState->navAreaRectAbove;
		navAreaRectBelow = fTrackState->navAreaRectBelow;
	}

	position = ConvertToScreen(position);
	printf("position: "); position.PrintToStream();

	if (!navAreaRectAbove.IsValid() && !navAreaRectBelow.IsValid() && fSelected != NULL && fSelected->Submenu() != NULL) {
		printf("_UpdateNavigationArea\n");
		_UpdateNavigationArea(position, navAreaRectAbove,
			navAreaRectBelow);
	}

	printf("navAreaRectAbove: "); navAreaRectAbove.PrintToStream();
	printf("navAreaRectBelow: "); navAreaRectBelow.PrintToStream();

	bool inNavArea = false;
	bool inNavAreaRectAbove = navAreaRectAbove.Contains(position);
	bool inNavAreaRectBelow = navAreaRectBelow.Contains(position);

	if (inNavAreaRectAbove || inNavAreaRectBelow) {
		bool isLeft = ConvertFromScreen(navAreaRectAbove).left == 0;
		BPoint p1, p2;

		if (inNavAreaRectAbove) {
			if (!isLeft) {
				p1 = navAreaRectAbove.LeftBottom();
				p2 = navAreaRectAbove.RightTop();
			} else {
				p2 = navAreaRectAbove.RightBottom();
				p1 = navAreaRectAbove.LeftTop();
			}
		} else {
			if (!isLeft) {
				p2 = navAreaRectBelow.LeftTop();
				p1 = navAreaRectBelow.RightBottom();
			} else {
				p1 = navAreaRectBelow.RightTop();
				p2 = navAreaRectBelow.LeftBottom();
			}
		}
		inNavArea =
			  (p1.y - p2.y) * position.x + (p2.x - p1.x) * position.y
			+ (p1.x - p2.x) * p1.y + (p2.y - p1.y) * p1.x >= 0;
	}

	printf("inNavArea: %d\n", inNavArea);

	if (!inNavArea && !(item != NULL && item->Frame().Contains(ConvertFromScreen(position)))) {
		printf("reset nav area\n");
		navAreaRectAbove = BRect();
		navAreaRectBelow = BRect();
	}

	{
		AutoLocker<BLocker> locker(fTrackState->locker);
		navAreaRectAbove = fTrackState->navAreaRectAbove = navAreaRectAbove;
		navAreaRectBelow = fTrackState->navAreaRectBelow = navAreaRectBelow;
	}

	return inNavArea;
}


void
BMenu::_UpdateStateOpenSelect(BMenuItem* item, BPoint position,
	BRect& navAreaRectAbove, BRect& navAreaRectBelow, bool isTimeout)
{
	if (fLayout != B_ITEMS_IN_COLUMN) {
		_SelectItem(item, true);
		return;
	}
	if (item != fSelected) {
		position = ConvertToScreen(position);

		bool inNavAreaRectAbove = navAreaRectAbove.Contains(position);
		bool inNavAreaRectBelow = navAreaRectBelow.Contains(position);

		if (fSelected == NULL
			|| (!inNavAreaRectAbove && !inNavAreaRectBelow)) {
			_SelectItem(item, true);
			navAreaRectAbove = BRect();
			navAreaRectBelow = BRect();
			{
				AutoLocker<BLocker> locker(fTrackState->locker);
				fTrackState->navigationAreaTimer.Unset();
			}
			return;
		}

		bool isLeft = ConvertFromScreen(navAreaRectAbove).left == 0;
		BPoint p1, p2;

		if (inNavAreaRectAbove) {
			if (!isLeft) {
				p1 = navAreaRectAbove.LeftBottom();
				p2 = navAreaRectAbove.RightTop();
			} else {
				p2 = navAreaRectAbove.RightBottom();
				p1 = navAreaRectAbove.LeftTop();
			}
		} else {
			if (!isLeft) {
				p2 = navAreaRectBelow.LeftTop();
				p1 = navAreaRectBelow.RightBottom();
			} else {
				p1 = navAreaRectBelow.RightTop();
				p2 = navAreaRectBelow.LeftBottom();
			}
		}
		bool inNavArea =
			  (p1.y - p2.y) * position.x + (p2.x - p1.x) * position.y
			+ (p1.x - p2.x) * p1.y + (p2.y - p1.y) * p1.x >= 0;

		if (!inNavArea || isTimeout) {
			_SelectItem(item, true);

			if (inNavArea) {
				_UpdateNavigationArea(position, navAreaRectAbove,
					navAreaRectBelow);
			} else {
				navAreaRectAbove = BRect();
				navAreaRectBelow = BRect();
			}

			{
				AutoLocker<BLocker> locker(fTrackState->locker);
				fTrackState->navigationAreaTimer.Unset();
			}
		}
	} else if (fSelected->Submenu() != NULL) {
		{
			AutoLocker<BLocker> locker(fTrackState->locker);
			BMessage message(navigationAreaTimeoutMsg);
			fTrackState->navigationAreaTimer.SetTo(new(std::nothrow) BMessageRunner(BMessenger(this), BMessage(navigationAreaTimeoutMsg), kNavigationAreaTimeout, 1));
		}

		_SelectItem(fSelected, true);

		if (!navAreaRectAbove.IsValid() && !navAreaRectBelow.IsValid()) {
			position = ConvertToScreen(position);
			_UpdateNavigationArea(position, navAreaRectAbove,
				navAreaRectBelow);
		}
	}
}


bool
BMenu::_AddItem(BMenuItem* item, int32 index)
{
	ASSERT(item != NULL);
	if (index < 0 || index > fItems.CountItems())
		return false;

	if (item->IsMarked())
		_ItemMarked(item);

	if (!fItems.AddItem(item, index))
		return false;

	// install the item on the supermenu's window
	// or onto our window, if we are a root menu
	BWindow* window = NULL;
	if (Superitem() != NULL)
		window = Superitem()->fWindow;
	else
		window = Window();
	if (window != NULL)
		item->Install(window);

	item->SetSuper(this);
	return true;
}


bool
BMenu::_RemoveItems(int32 index, int32 count, BMenuItem* item,
	bool deleteItems)
{
	bool success = false;
	bool invalidateLayout = false;

	bool locked = LockLooper();
	BWindow* window = Window();

	// The plan is simple: If we're given a BMenuItem directly, we use it
	// and ignore index and count. Otherwise, we use them instead.
	if (item != NULL) {
		if (fItems.RemoveItem(item)) {
			if (item == fSelected && window != NULL)
				_SelectItem(NULL);
			item->Uninstall();
			item->SetSuper(NULL);
			if (deleteItems)
				delete item;
			success = invalidateLayout = true;
		}
	} else {
		// We iterate backwards because it's simpler
		int32 i = std::min(index + count - 1, fItems.CountItems() - 1);
		// NOTE: the range check for "index" is done after
		// calculating the last index to be removed, so
		// that the range is not "shifted" unintentionally
		index = std::max((int32)0, index);
		for (; i >= index; i--) {
			item = static_cast<BMenuItem*>(fItems.ItemAt(i));
			if (item != NULL) {
				if (fItems.RemoveItem(i)) {
					if (item == fSelected && window != NULL)
						_SelectItem(NULL);
					item->Uninstall();
					item->SetSuper(NULL);
					if (deleteItems)
						delete item;
					success = true;
					invalidateLayout = true;
				} else {
					// operation not entirely successful
					success = false;
					break;
				}
			}
		}
	}

	if (invalidateLayout) {
		InvalidateLayout();
		if (locked && window != NULL) {
			_LayoutItems(0);
			_UpdateWindowViewSize(false);
			Invalidate();
		}
	}

	if (locked)
		UnlockLooper();

	return success;
}


bool
BMenu::_RelayoutIfNeeded()
{
	if (!fUseCachedMenuLayout) {
		fUseCachedMenuLayout = true;
		_CacheFontInfo();
		_LayoutItems(0);
		_UpdateWindowViewSize(false);
		return true;
	}
	return false;
}


void
BMenu::_LayoutItems(int32 index)
{
	_CalcTriggers();

	float width;
	float height;
	_ComputeLayout(index, fResizeToFit, true, &width, &height);

	if (fResizeToFit)
		ResizeTo(width, height);
}


BSize
BMenu::_ValidatePreferredSize()
{
	if (!fLayoutData->preferred.IsWidthSet() || ResizingMode()
			!= fLayoutData->lastResizingMode) {
		_ComputeLayout(0, true, false, NULL, NULL);
		ResetLayoutInvalidation();
	}

	return fLayoutData->preferred;
}


void
BMenu::_ComputeLayout(int32 index, bool bestFit, bool moveItems,
	float* _width, float* _height)
{
	// TODO: Take "bestFit", "moveItems", "index" into account,
	// Recalculate only the needed items,
	// not the whole layout every time

	fLayoutData->lastResizingMode = ResizingMode();

	BRect frame;
	switch (fLayout) {
		case B_ITEMS_IN_COLUMN:
		{
			BRect parentFrame;
			BRect* overrideFrame = NULL;
			if (dynamic_cast<_BMCMenuBar_*>(Supermenu()) != NULL) {
				// When the menu is modified while it's open, we get here in a
				// situation where trying to lock the looper would deadlock
				// (the window is locked waiting for the menu to terminate).
				// In that case, just give up on getting the supermenu bounds
				// and keep the menu at the current width and position.
				if (Supermenu()->LockLooperWithTimeout(0) == B_OK) {
					parentFrame = Supermenu()->Bounds();
					Supermenu()->UnlockLooper();
					overrideFrame = &parentFrame;
				}
			}

			_ComputeColumnLayout(index, bestFit, moveItems, overrideFrame,
				frame);
			break;
		}

		case B_ITEMS_IN_ROW:
			_ComputeRowLayout(index, bestFit, moveItems, frame);
			break;

		case B_ITEMS_IN_MATRIX:
			_ComputeMatrixLayout(frame);
			break;
	}

	// change width depending on resize mode
	BSize size;
	if ((ResizingMode() & B_FOLLOW_LEFT_RIGHT) == B_FOLLOW_LEFT_RIGHT) {
		if (dynamic_cast<_BMCMenuBar_*>(this) != NULL)
			size.width = Bounds().Width() - fPad.right;
		else if (Parent() != NULL)
			size.width = Parent()->Frame().Width();
		else if (Window() != NULL)
			size.width = Window()->Frame().Width();
		else
			size.width = Bounds().Width();
	} else
		size.width = frame.Width();

	size.height = frame.Height();

	if (_width)
		*_width = size.width;

	if (_height)
		*_height = size.height;

	if (bestFit)
		fLayoutData->preferred = size;

	if (moveItems)
		fUseCachedMenuLayout = true;
}


void
BMenu::_ComputeColumnLayout(int32 index, bool bestFit, bool moveItems,
	BRect* overrideFrame, BRect& frame)
{
	bool command = false;
	bool control = false;
	bool shift = false;
	bool option = false;
	bool submenu = false;

	if (index > 0)
		frame = ItemAt(index - 1)->Frame();
	else if (overrideFrame != NULL)
		frame.Set(0, 0, overrideFrame->right, -1);
	else
		frame.Set(0, 0, 0, -1);

	BFont font;
	GetFont(&font);

	// Loop over all items to set their top, bottom and left coordinates,
	// all while computing the width of the menu
	for (; index < fItems.CountItems(); index++) {
		BMenuItem* item = ItemAt(index);

		float width;
		float height;
		item->GetContentSize(&width, &height);

		if (item->fModifiers && item->fShortcutChar) {
			width += font.Size();
			if ((item->fModifiers & B_COMMAND_KEY) != 0)
				command = true;

			if ((item->fModifiers & B_CONTROL_KEY) != 0)
				control = true;

			if ((item->fModifiers & B_SHIFT_KEY) != 0)
				shift = true;

			if ((item->fModifiers & B_OPTION_KEY) != 0)
				option = true;
		}

		item->fBounds.left = 0.0f;
		item->fBounds.top = frame.bottom + 1.0f;
		item->fBounds.bottom = item->fBounds.top + height + fPad.top
			+ fPad.bottom;

		if (item->fSubmenu != NULL)
			submenu = true;

		frame.right = std::max(frame.right, width + fPad.left + fPad.right);
		frame.bottom = item->fBounds.bottom;
	}

	// Compute the extra space needed for shortcuts and submenus
	if (command) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemCommand()->Bounds().Width() + 1;
	}
	if (control) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemControl()->Bounds().Width() + 1;
	}
	if (option) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemOption()->Bounds().Width() + 1;
	}
	if (shift) {
		frame.right
			+= BPrivate::MenuPrivate::MenuItemShift()->Bounds().Width() + 1;
	}
	if (submenu) {
		frame.right += ItemAt(0)->Frame().Height() / 2;
		fHasSubmenus = true;
	} else {
		fHasSubmenus = false;
	}

	if (fMaxContentWidth > 0)
		frame.right = std::min(frame.right, fMaxContentWidth);

	frame.top = 0;
	frame.right = ceilf(frame.right);

	// Finally update the "right" coordinate of all items
	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.right = frame.right;
	}
}


void
BMenu::_ComputeRowLayout(int32 index, bool bestFit, bool moveItems,
	BRect& frame)
{
	font_height fh;
	GetFontHeight(&fh);
	frame.Set(0.0f, 0.0f, 0.0f, ceilf(fh.ascent + fh.descent + fPad.top
		+ fPad.bottom));

	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		float width, height;
		item->GetContentSize(&width, &height);

		item->fBounds.left = frame.right;
		item->fBounds.top = 0.0f;
		item->fBounds.right = item->fBounds.left + width + fPad.left
			+ fPad.right;

		frame.right = item->Frame().right + 1.0f;
		frame.bottom = std::max(frame.bottom, height + fPad.top + fPad.bottom);
	}

	if (moveItems) {
		for (int32 i = 0; i < fItems.CountItems(); i++)
			ItemAt(i)->fBounds.bottom = frame.bottom;
	}

	if (bestFit)
		frame.right = ceilf(frame.right);
	else
		frame.right = Bounds().right;
}


void
BMenu::_ComputeMatrixLayout(BRect &frame)
{
	frame.Set(0, 0, 0, 0);
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item != NULL) {
			frame.left = std::min(frame.left, item->Frame().left);
			frame.right = std::max(frame.right, item->Frame().right);
			frame.top = std::min(frame.top, item->Frame().top);
			frame.bottom = std::max(frame.bottom, item->Frame().bottom);
		}
	}
}


void
BMenu::LayoutInvalidated(bool descendants)
{
	fUseCachedMenuLayout = false;
	fLayoutData->preferred.Set(B_SIZE_UNSET, B_SIZE_UNSET);
}


// Assumes the SuperMenu to be locked (due to calling ConvertToScreen())
BPoint
BMenu::ScreenLocation()
{
	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	if (superMenu == NULL || superItem == NULL) {
		debugger("BMenu can't determine where to draw."
			"Override BMenu::ScreenLocation() to determine location.");
	}

	BPoint point;
	if (superMenu->Layout() == B_ITEMS_IN_COLUMN)
		point = superItem->Frame().RightTop() + BPoint(1.0f, 0.0f);
	else
		point = superItem->Frame().LeftBottom() + BPoint(1.0f, 1.0f);

	superMenu->ConvertToScreen(&point);

	return point;
}


BRect
BMenu::_CalcFrame(BPoint where, bool* scrollOn)
{
	// TODO: Improve me
	BRect bounds = Bounds();
	BRect frame = bounds.OffsetToCopy(where);

	BScreen screen(Window());
	BRect screenFrame = screen.Frame();

	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	// Reset frame shifted state since this menu is being redrawn
	fExtraMenuData->frameShiftedLeft = false;

	// TODO: Horrible hack:
	// When added to a BMenuField, a BPopUpMenu is the child of
	// a _BMCMenuBar_ to "fake" the menu hierarchy
	bool inMenuField = dynamic_cast<_BMCMenuBar_*>(superMenu) != NULL;

	// Offset the menu field menu window left by the width of the checkmark
	// so that the text when the menu is closed lines up with the text when
	// the menu is open.
	if (inMenuField)
		frame.OffsetBy(-8.0f, 0.0f);

	if (superMenu == NULL || superItem == NULL || inMenuField) {
		// just move the window on screen
		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
		else if (frame.top < screenFrame.top)
			frame.OffsetBy(0, -frame.top);

		if (frame.right > screenFrame.right) {
			frame.OffsetBy(screenFrame.right - frame.right, 0);
			fExtraMenuData->frameShiftedLeft = true;
		}
		else if (frame.left < screenFrame.left)
			frame.OffsetBy(-frame.left, 0);
	} else if (superMenu->Layout() == B_ITEMS_IN_COLUMN) {
		if (frame.right > screenFrame.right
				|| superMenu->fExtraMenuData->frameShiftedLeft) {
			frame.OffsetBy(-superItem->Frame().Width() - frame.Width() - 2, 0);
			fExtraMenuData->frameShiftedLeft = true;
		}

		if (frame.left < 0)
			frame.OffsetBy(-frame.left + 6, 0);

		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	} else {
		if (frame.bottom > screenFrame.bottom) {
			float spaceBelow = screenFrame.bottom - frame.top;
			float spaceOver = frame.top - screenFrame.top
				- superItem->Frame().Height();
			if (spaceOver > spaceBelow) {
				frame.OffsetBy(0, -superItem->Frame().Height()
					- frame.Height() - 3);
			}
		}

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
	}

	if (scrollOn != NULL) {
		// basically, if this returns false, it means
		// that the menu frame won't fit completely inside the screen
		// TODO: Scrolling will currently only work up/down,
		// not left/right
		*scrollOn = screenFrame.top > frame.top
			|| screenFrame.bottom < frame.bottom;
	}

	return frame;
}


void
BMenu::DrawItems(BRect updateRect)
{
	int32 itemCount = fItems.CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Frame().Intersects(updateRect))
			item->Draw();
	}
}


void
BMenu::_InvokeItem(BMenuItem* item, bool now)
{
	if (!item->IsEnabled())
		return;

	// called from BWindow for shortcut handling
	if (now) {
		// Lock the root menu window before calling BMenuItem::Invoke()
		BMenu* parent = this;
		BMenu* rootMenu = NULL;
		do {
			rootMenu = parent;
			parent = rootMenu->Supermenu();
		} while (parent != NULL);

		if (rootMenu->LockLooper()) {
			item->Invoke();
			rootMenu->UnlockLooper();
		}
		return;
	}

	{
		if (fTrackState == NULL)
			return;
		AutoLocker<BLocker> locker(fTrackState->locker);
		if (fTrackState->invokedItem != NULL)
			return;
		fTrackState->invokedItem = item;
	}

	// Do the "selected" animation
	#if 0
	if (LockLooper()) {
		snooze(50000);
		item->Select(true);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(false);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(true);
		Window()->UpdateIfNeeded();
		snooze(50000);
		item->Select(false);
		Window()->UpdateIfNeeded();
		UnlockLooper();
	}
	#endif
}


bool
BMenu::_OverSuper(BPoint location)
{
	if (!Supermenu())
		return false;

	return fSuperbounds.Contains(location);
}


bool
BMenu::_OverSubmenu(BMenuItem* item, BPoint loc)
{
	if (item == NULL)
		return false;

	BMenu* subMenu = item->Submenu();
	if (subMenu == NULL || subMenu->Window() == NULL)
		return false;

	// assume that loc is in screen coordinates
	if (subMenu->Window()->Frame().Contains(loc))
		return true;

	return subMenu->_OverSubmenu(subMenu->fSelected, loc);
}


BMenuWindow*
BMenu::_MenuWindow()
{
#if USE_CACHED_MENUWINDOW
	if (fCachedMenuWindow == NULL) {
		char windowName[64];
		snprintf(windowName, 64, "%s cached menu", Name());
		fCachedMenuWindow = new (nothrow) BMenuWindow(windowName);
	}
#endif
	return fCachedMenuWindow;
}


void
BMenu::_DeleteMenuWindow()
{
	if (fCachedMenuWindow != NULL) {
		fCachedMenuWindow->Lock();
		fCachedMenuWindow->Quit();
		fCachedMenuWindow = NULL;
	}
}


BMenuItem*
BMenu::_HitTestItems(BPoint where, BPoint slop) const
{
	// TODO: Take "slop" into account ?

	// if the point doesn't lie within the menu's
	// bounds, bail out immediately
	if (!Bounds().Contains(where))
		return NULL;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Frame().Contains(where)
			&& dynamic_cast<BSeparatorItem*>(item) == NULL) {
			return item;
		}
	}

	return NULL;
}


BRect
BMenu::_Superbounds() const
{
	return fSuperbounds;
}


void
BMenu::_CacheFontInfo()
{
	font_height fh;
	GetFontHeight(&fh);
	fAscent = fh.ascent;
	fDescent = fh.descent;
	fFontHeight = ceilf(fh.ascent + fh.descent + fh.leading);
}


void
BMenu::_ItemMarked(BMenuItem* item)
{
	if (IsRadioMode()) {
		for (int32 i = 0; i < CountItems(); i++) {
			if (ItemAt(i) != item)
				ItemAt(i)->SetMarked(false);
		}
	}

	if (IsLabelFromMarked() && Superitem() != NULL)
		Superitem()->SetLabel(item->Label());
}


void
BMenu::_Install(BWindow* target)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Install(target);
}


void
BMenu::_Uninstall()
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Uninstall();
}


void
BMenu::_SelectItem(BMenuItem* item, bool showSubmenu, bool selectFirstItem)
{
	// Avoid deselecting and then reselecting the same item
	// which would cause flickering
	if (item != fSelected) {
		if (fSelected != NULL) {
			fSelected->Select(false);
			BMenu* subMenu = fSelected->Submenu();
			if (subMenu != NULL && subMenu->Window() != NULL)
				subMenu->_Hide();
		}

		fSelected = item;
		if (fSelected != NULL) {
			BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
			if (window != NULL && window->LockLooper()) {
				BRect frame = ConvertToParent(fSelected->Frame());
				float height = Parent()->Bounds().Height();
				if (frame.top < 0)
					window->TryScrollBy(frame.top);
				else if (frame.bottom > height)
					window->TryScrollBy(frame.bottom - height);
				window->UnlockLooper();
			}
			fSelected->Select(true);
		}
	}

	if (fSelected != NULL && showSubmenu) {
		BMenu* subMenu = fSelected->Submenu();
		if (subMenu != NULL && subMenu->Window() == NULL) {
			if (!subMenu->_Show(selectFirstItem)) {
				// something went wrong, deselect the item
				printf("_SelectItem: can't show submenu\n");
				fSelected->Select(false);
				fSelected = NULL;
			}
		}
	}
}


bool
BMenu::_SelectNextItem(BMenuItem* item, bool forward)
{
	if (CountItems() == 0) // cannot select next item in an empty menu
		return false;

	BMenuItem* nextItem = _NextItem(item, forward);
	if (nextItem == NULL)
		return false;

	_SelectItem(nextItem, true, false);

	if (LockLooper()) {
		be_app->ObscureCursor();
		UnlockLooper();
	}

	return true;
}


BMenuItem*
BMenu::_NextItem(BMenuItem* item, bool forward) const
{
	const int32 numItems = fItems.CountItems();
	if (numItems == 0)
		return NULL;

	int32 index = fItems.IndexOf(item);
	int32 loopCount = numItems;
	while (loopCount--) {
		// Cycle through menu items in the given direction...
		if (forward)
			index++;
		else
			index--;

		// ... wrap around...
		if (index < 0)
			index = numItems - 1;
		else if (index >= numItems)
			index = 0;

		// ... and return the first suitable item found.
		BMenuItem* nextItem = ItemAt(index);
		if (nextItem->IsEnabled())
			return nextItem;
	}

	// If no other suitable item was found, return NULL.
	return NULL;
}


void
BMenu::_SetStickyMode(bool sticky)
{
	if (fTrackState == NULL) {
		fStickyMode = sticky;
		return;
	}
	AutoLocker<BLocker> locker(fTrackState->locker);
	fTrackState->rootMenu->fStickyMode = sticky;
}


bool
BMenu::_IsStickyMode() const
{
	if (fTrackState == NULL) {
		return fStickyMode;
	}
	AutoLocker<BLocker> locker(fTrackState->locker);
	return fTrackState->rootMenu->fStickyMode;
}


void
BMenu::_GetShiftKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_SHIFT_KEY, &value) != B_OK)
		value = 0x4b;
}


void
BMenu::_GetControlKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_CONTROL_KEY, &value) != B_OK)
		value = 0x5c;
}


void
BMenu::_GetCommandKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_COMMAND_KEY, &value) != B_OK)
		value = 0x66;
}


void
BMenu::_GetOptionKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_LEFT_OPTION_KEY, &value) != B_OK)
		value = 0x5d;
}


void
BMenu::_GetMenuKey(uint32 &value) const
{
	// TODO: Move into init_interface_kit().
	// Currently we can't do that, as get_modifier_key() blocks forever
	// when called on input_server initialization, since it tries
	// to send a synchronous message to itself (input_server is
	// a BApplication)

	if (get_modifier_key(B_MENU_KEY, &value) != B_OK)
		value = 0x68;
}


void
BMenu::_CalcTriggers()
{
	BPrivate::TriggerList triggerList;

	// Gathers the existing triggers set by the user
	for (int32 i = 0; i < CountItems(); i++) {
		char trigger = ItemAt(i)->Trigger();
		if (trigger != 0)
			triggerList.AddTrigger(trigger);
	}

	// Set triggers for items which don't have one yet
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Trigger() == 0) {
			uint32 trigger;
			int32 index;
			if (_ChooseTrigger(item->Label(), index, trigger, triggerList))
				item->SetAutomaticTrigger(index, trigger);
		}
	}
}


bool
BMenu::_ChooseTrigger(const char* title, int32& index, uint32& trigger,
	BPrivate::TriggerList& triggers)
{
	if (title == NULL)
		return false;

	index = 0;
	uint32 c;
	const char* nextCharacter, *character;

	// two runs: first we look out for alphanumeric ASCII characters
	nextCharacter = title;
	character = nextCharacter;
	while ((c = BUnicodeChar::FromUTF8(&nextCharacter)) != 0) {
		if (!(c < 128 && BUnicodeChar::IsAlNum(c)) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = BUnicodeChar::ToLower(c);
		index = (int32)(character - title);
		return triggers.AddTrigger(c);
	}

	// then, if we still haven't found something, we accept anything
	nextCharacter = title;
	character = nextCharacter;
	while ((c = BUnicodeChar::FromUTF8(&nextCharacter)) != 0) {
		if (BUnicodeChar::IsSpace(c) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = BUnicodeChar::ToLower(c);
		index = (int32)(character - title);
		return triggers.AddTrigger(c);
	}

	return false;
}


void
BMenu::_UpdateWindowViewSize(const bool &move)
{
	BMenuWindow* window = dynamic_cast<BMenuWindow*>(Window());
	if (window == NULL)
		return;

	if (!fResizeToFit)
		return;

	bool scroll = false;
	const BPoint screenLocation = move ? ScreenLocation()
		: window->Frame().LeftTop();
	BRect frame = _CalcFrame(screenLocation, &scroll);
	ResizeTo(frame.Width(), frame.Height());

	if (fItems.CountItems() > 0) {
		if (!scroll) {
			if (fLayout == B_ITEMS_IN_COLUMN)
				window->DetachScrollers();

			window->ResizeTo(Bounds().Width(), Bounds().Height());
		} else {

			// Resize the window to fit the screen without overflowing the
			// frame, and attach scrollers to our cached BMenuWindow.
			BScreen screen(window);
			frame = frame & screen.Frame();
			window->ResizeTo(Bounds().Width(), frame.Height());

			// we currently only support scrolling for B_ITEMS_IN_COLUMN
			if (fLayout == B_ITEMS_IN_COLUMN) {
				window->AttachScrollers();

				BMenuItem* selectedItem = FindMarked();
				if (selectedItem != NULL) {
					// scroll to the selected item
					if (Supermenu() == NULL) {
						window->TryScrollTo(selectedItem->Frame().top);
					} else {
						BPoint point = selectedItem->Frame().LeftTop();
						BPoint superPoint = Superitem()->Frame().LeftTop();
						Supermenu()->ConvertToScreen(&superPoint);
						ConvertToScreen(&point);
						window->TryScrollTo(point.y - superPoint.y);
					}
				}
			}
		}
	} else {
		_CacheFontInfo();
		window->ResizeTo(StringWidth(BPrivate::kEmptyMenuLabel)
				+ fPad.left + fPad.right,
			fFontHeight + fPad.top + fPad.bottom);
	}

	if (move)
		window->MoveTo(frame.LeftTop());
}


bool
BMenu::_AddDynamicItems()
{
	printf("_AddDynamicItems(%p)\n", this);
	bool addAborted = false;
	if (AddDynamicItem(B_INITIAL_ADD)) {
		BMenuItem* superItem = Superitem();
		BMenu* superMenu = Supermenu();
		do {
			//printf("_AddDynamicItems: step\n");
			if (superMenu != NULL
				&& !superMenu->_OkToProceed(superItem)) {
				AddDynamicItem(B_ABORT);
				addAborted = true;
				break;
			}
		} while (AddDynamicItem(B_PROCESSING));
	}

	return addAborted;
}


bool
BMenu::_OkToProceed(BMenuItem* item)
{
	return true; /* !!! */
}


void
BMenu::_CallTrackingHook()
{
	if (fExtraMenuData != NULL && fExtraMenuData->trackingHook != NULL
		&& fExtraMenuData->trackingState != NULL) {
		if (fExtraMenuData->trackingHook(this, fExtraMenuData->trackingState))
			_QuitTracking(true);
	}
}


void
BMenu::_QuitTracking(bool onlyThis)
{
	if (fTrackState == NULL)
		return;

	AutoLocker<BLocker> locker(fTrackState->locker);

	if (onlyThis && Supermenu() != NULL) {
		_SelectItem(NULL);
		fTrackState->curMenu = Supermenu();
		return;
	}

	if (!fTrackState->quit) {
		fTrackState->quit = true;
		send_data(fTrackState->trackThread, MENU_TRACK_CMD_DONE, NULL, 0);
	}
}


//	#pragma mark - menu_info functions


// TODO: Maybe the following two methods would fit better into
// InterfaceDefs.cpp
// In R5, they do all the work client side, we let the app_server handle the
// details.
status_t
set_menu_info(menu_info* info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_MENU_INFO);
	link.Attach<menu_info>(*info);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		BMenu::sMenuInfo = *info;
		// Update also the local copy, in case anyone relies on it

	return status;
}


status_t
get_menu_info(menu_info* info)
{
	if (!info)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MENU_INFO);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<menu_info>(info);

	return status;
}


extern "C" void
B_IF_GCC_2(InvalidateLayout__5BMenub,_ZN5BMenu16InvalidateLayoutEb)(
	BMenu* menu, bool descendants)
{
	menu->InvalidateLayout();
}
