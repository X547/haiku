
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/glibc/misc/posix_gnu_misc.o',
	[
		'efgcvt.c',
		'efgcvt_r.c',
	],
	cpp_args: [
		'USE_IN_LIBIO',
		'_GNU_SOURCE',
	],
	include_directories: [
		'headers/private',
		'headers/private/libroot',
		'src/system/libroot/posix/glibc',
		'src/system/libroot/posix/glibc/ctype',
		'src/system/libroot/posix/glibc/include',
		'src/system/libroot/posix/glibc/include/arch/generic',
		'src/system/libroot/posix/glibc/include/arch/x86_64',
		'src/system/libroot/posix/glibc/libio',
		'src/system/libroot/posix/glibc/locale',
		'src/system/libroot/posix/glibc/misc',
	],
	include_directories: [
		'.',
	],
)
