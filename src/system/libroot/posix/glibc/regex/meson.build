
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/glibc/regex/posix_gnu_regex.o',
	[
		'regex.c',
	],
	cpp_args: [
		'HAVE_STDBOOL_H',
		'_REGEX_RE_COMP',
		'__USE_GNU',
	],
	include_directories: [
		'headers/private',
	],
	include_directories: [
		'.',
	],
)
