project('app_server', 'cpp',
	default_options: ['warning_level=3', 'cpp_std=c++2a']
)

cpp = meson.get_compiler('cpp')

dep_be = cpp.find_library('be')
dep_bnetapi = cpp.find_library('bnetapi')
dep_shared = cpp.find_library('shared')
dep_freetype2 = dependency('freetype2')

hdr_app_server = include_directories('.')

hdr_private = include_directories('/boot/system/develop/headers/private')
hdr_private_shared = include_directories('/boot/system/develop/headers/private/shared')
hdr_private_kernel = include_directories('/boot/system/develop/headers/private/kernel')
hdr_private_system = include_directories(
	'/boot/system/develop/headers/private/system',
	'/boot/system/develop/headers/private/system/arch/x86_64',
)
hdr_private_app = include_directories('/boot/system/develop/headers/private/app')
hdr_private_graphics = include_directories('/boot/system/develop/headers/private/graphics')
hdr_private_graphics_common = include_directories('/boot/system/develop/headers/private/graphics/common')
hdr_private_input = include_directories('/boot/system/develop/headers/private/input')
hdr_private_interface = include_directories('/boot/system/develop/headers/private/interface')
hdr_private_storage = include_directories('/boot/system/develop/headers/private/storage')
hdr_private_support = include_directories('/boot/system/develop/headers/private/support')

hdr_libs_agg = include_directories('libs/agg/headers')

subdir('libs')

executable('test_app_server',
	'Angle.cpp',
	'AppServer.cpp',
	'BitmapManager.cpp',
	'Canvas.cpp',
	'ClientMemoryAllocator.cpp',
	'CursorManager.cpp',
	'CursorSet.cpp',
	'DelayedMessage.cpp',
	'Desktop.cpp',
	'DesktopListener.cpp',
	'DesktopSettings.cpp',
	'DirectWindowInfo.cpp',
	'DrawState.cpp',
	'EventDispatcher.cpp',
	'EventStream.cpp',
	'InputManager.cpp',
	'IntPoint.cpp',
	'IntRect.cpp',
	'Layer.cpp',
	'MessageLooper.cpp',
	'MultiLocker.cpp',
	'OffscreenServerWindow.cpp',
	'OffscreenWindow.cpp',
	'PictureBoundingBoxPlayer.cpp',
	'ProfileMessageSupport.cpp',
	'RGBColor.cpp',
	'RegionPool.cpp',
	'Screen.cpp',
	'ScreenConfigurations.cpp',
	'ScreenManager.cpp',
	'ServerApp.cpp',
	'ServerBitmap.cpp',
	'ServerCursor.cpp',
	'ServerFont.cpp',
	'ServerPicture.cpp',
	'ServerWindow.cpp',
	'SystemPalette.cpp',
	'View.cpp',
	'VirtualScreen.cpp',
	'Window.cpp',
	'WindowList.cpp',
	'Workspace.cpp',
	'WorkspacesView.cpp',

	'decorator/DecorManager.cpp',
	'decorator/Decorator.cpp',
	'decorator/DefaultDecorator.cpp',
	'decorator/DefaultWindowBehaviour.cpp',
	'decorator/MagneticBorder.cpp',
	'decorator/TabDecorator.cpp',
	'decorator/WindowBehaviour.cpp',

	'drawing/AlphaMask.cpp',
	'drawing/AlphaMaskCache.cpp',
	'drawing/BBitmapBuffer.cpp',
	'drawing/BitmapBuffer.cpp',
	'drawing/BitmapDrawingEngine.cpp',
	'drawing/BitmapHWInterface.cpp',
	'drawing/DrawingEngine.cpp',
	'drawing/DrawingEngineBase.cpp',
	'drawing/HWInterface.cpp',
	'drawing/MallocBuffer.cpp',
	'drawing/Overlay.cpp',
	'drawing/PatternHandler.cpp',
	'drawing/drawing_support.cpp',

	'drawing/Painter/AGGTextRenderer.cpp',
	'drawing/Painter/GlobalSubpixelSettings.cpp',
	'drawing/Painter/Painter.cpp',
	'drawing/Painter/Transformable.cpp',
	'drawing/Painter/bitmap_painter/BitmapPainter.cpp',
	'drawing/Painter/drawing_modes/PixelFormat.cpp',

	'drawing/interface/local/AccelerantBuffer.cpp',
	'drawing/interface/local/AccelerantHWInterface.cpp',

	'drawing/interface/remote/NetReceiver.cpp',
	'drawing/interface/remote/NetSender.cpp',
	'drawing/interface/remote/RemoteDrawingEngine.cpp',
	'drawing/interface/remote/RemoteEventStream.cpp',
	'drawing/interface/remote/RemoteHWInterface.cpp',
	'drawing/interface/remote/RemoteMessage.cpp',
	'drawing/interface/remote/StreamingRingBuffer.cpp',

	'drawing/interface/virtual/DWindowBuffer.cpp',
	'drawing/interface/virtual/DWindowHWInterface.cpp',
	'drawing/interface/virtual/ViewHWInterface.cpp',

	'font/AppFontManager.cpp',
	'font/FontCache.cpp',
	'font/FontCacheEntry.cpp',
	'font/FontEngine.cpp',
	'font/FontFamily.cpp',
	'font/FontManager.cpp',
	'font/FontStyle.cpp',
	'font/GlobalFontManager.cpp',

	'stackandtile/SATDecorator.cpp',
	'stackandtile/SATGroup.cpp',
	'stackandtile/SATWindow.cpp',
	'stackandtile/StackAndTile.cpp',
	'stackandtile/Stacking.cpp',
	'stackandtile/Tiling.cpp',

	implicit_include_directories: false,
	cpp_args: [
		'-DTEST_MODE',
		'-DHAIKU_TARGET_PLATFORM_LIBBE_TEST',
		'-iquote', meson.current_source_dir(),
		'-iquote', meson.current_source_dir() / 'font',
		'-iquote', meson.current_source_dir() / 'drawing',
		'-iquote', meson.current_source_dir() / 'drawing' / 'Painter',
		'-iquote', meson.current_source_dir() / 'drawing' / 'Painter' / 'bitmap_painter',
		'-iquote', meson.current_source_dir() / 'drawing' / 'Painter' / 'drawing_modes',
		'-iquote', meson.current_source_dir() / 'drawing' / 'interface' / 'local',
		'-iquote', meson.current_source_dir() / 'drawing' / 'interface' / 'remote',
		'-iquote', meson.current_source_dir() / 'drawing' / 'interface' / 'virtual',
		'-iquote', meson.current_source_dir() / 'stackandtile',
		'-iquote', meson.current_source_dir() / 'decorator',
	],
	include_directories: [
		hdr_private,
		hdr_private_shared,
		hdr_private_kernel,
		hdr_private_system,
		hdr_private_app,
		hdr_private_graphics,
		hdr_private_graphics_common,
		hdr_private_input,
		hdr_private_interface,
		hdr_private_storage,
		hdr_private_support,
		hdr_linprog,
	],
	dependencies: [
		dep_be,
		dep_bnetapi,
		dep_shared,
		dep_freetype2,
		dep_agg,
		dep_linprog,
	],
	gnu_symbol_visibility: 'hidden',
	install: true
)
