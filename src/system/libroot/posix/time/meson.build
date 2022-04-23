
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/time/posix_time.o',
	[
		'asctime.cpp',
		'clock.cpp',
		'clock_support.cpp',
		'ctime.c',
		'localtime.cpp',
		'localtime_fading_out.c',
		'nanosleep.c',
		'stime.c',
		'time.c',
		'timer_support.cpp',
		'timespec_get.cpp',
	],
	cpp_args: [
		'NOID',
		'PCTS=1',
		'STD_INSPIRED',
		'TM_GMTOFF=tm_gmtoff',
		'TM_ZONE=tm_zone',
		'USG_COMPAT',
	],
	include_directories: [
		'headers/private',
		'headers/private/libroot',
		'headers/private/libroot/locale',
		'headers/private/libroot/time',
		'headers/private/shared',
		'headers/private/system',
		'headers/private/system/arch/x86_64',
	],
	include_directories: [
		'.',
	],
)
