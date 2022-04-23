subdir('app')
subdir('interface')
subdir('locale')
subdir('shared')
subdir('storage')
subdir('support')


libbe = shared_library(
	'be',
	link_whole: [
		libapp_kit,
		libinterface_kit,
		liblocale_kit,
		libstorage_kit,
		libsupport_kit,
	],
	link_with: [
		libshared, libicon
	],
	link_args: ['-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), 'libbe_versions')],
	dependencies: [
		dependency('zlib'),
		dependency('libzstd'),
		dependency('icu-i18n'),
	]
)
