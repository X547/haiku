posix_gnu_regex = static_library(
	'posix_gnu_regex.o',
	'regex.c',
	cpp_args: [
		'HAVE_STDBOOL_H',
		'_REGEX_RE_COMP',
		'__USE_GNU',
	],
	include_directories: [
	],
)
