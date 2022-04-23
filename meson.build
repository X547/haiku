project(
	'haiku', 'cpp', 'c',
  version : '0.1',
  default_options : ['warning_level=0', 'cpp_std=c++2a']
 )

cpp = meson.get_compiler('cpp')

# disable some warnings
add_project_arguments('-Wno-multichar', language: ['c', 'cpp'])
add_project_arguments('-Wno-register', language: ['c', 'cpp'])
if meson.get_compiler('cpp').get_id() == 'clang'
	add_project_arguments('-Wno-non-c-typedef-for-linkage', language: 'cpp')
	add_project_arguments('-Wno-return-type-c-linkage', language: 'cpp')
endif

add_project_arguments([
	'-DARCH_x86_64',
	'-D__HAIKU_PRIMARY_PACKAGING_ARCH="x86_64"',
	'-DHAIKU_DISTRO_COMPATIBILITY_OFFICIAL',
	'-DHAIKU_TARGET_PLATFORM_HAIKU',
	'-DHAIKU_REGULAR_BUILD',
], language: ['c', 'cpp'])

hdr_base = include_directories([
	'build/config_headers',
	'headers',
	'headers/os',
	'headers/compatibility/bsd',
	'headers/posix',
	'headers/glibc',
#	'headers/cpp',
	'headers/os/app',
	'headers/os/device',
	'headers/os/interface',
	'headers/os/locale',
	'headers/os/media',
	'headers/os/midi',
	'headers/os/midi2',
	'headers/os/net',
	'headers/os/kernel',
	'headers/os/storage',
	'headers/os/support',
	'headers/os/game',
#	'headers/os/opengl',
	'headers/os/drivers',
#	'headers/gnu',
	'headers/os/mail',
	'headers/os/translation',
#	'headers/os/devel',
	'headers/os/add-ons/graphics',
	'headers/os/be_apps/NetPositive',
	'headers/os/be_apps/Tracker',
	'headers/private',
])

hdr_private = include_directories('headers/private')
hdr_private_app = include_directories('headers/private/app')
hdr_private_shared = include_directories('headers/private/shared')
hdr_private_kernel = include_directories('headers/private/kernel', 'headers/private/kernel/arch/x86')
hdr_private_interface = include_directories('headers/private/interface')
hdr_private_system = include_directories(['headers/private/system', 'headers/private/system/arch/x86_64'])
hdr_private_libroot = include_directories('headers/private/libroot')
hdr_private_locale = include_directories('headers/private/locale')
hdr_private_support = include_directories('headers/private/support')
hdr_private_input = include_directories('headers/private/input')
hdr_private_print = include_directories('headers/private/print')
hdr_private_runtimeloader = include_directories('headers/private/runtime_loader')
hdr_private_storage = include_directories('headers/private/storage')
hdr_private_tracker = include_directories('headers/private/tracker')
hdr_private_media = include_directories('headers/private/media')

hdr_libs_mapm = include_directories('headers/libs/mapm')
hdr_libs_agg = include_directories('headers/libs/agg')

prog_keymap = find_program('keymap')

system_keymap_h = custom_target(
	output: 'SystemKeymap.h',
	input: 'src/data/keymaps/US-International.keymap',
	command: [prog_keymap, '-o', '@OUTPUT@', '-h', '@INPUT@'],
)

subdir('src')
