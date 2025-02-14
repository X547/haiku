hdr_libroot_musl = include_directories('include')

subdir('math')

libposix_musl = static_library('posix_musl',
	link_whole: [
		libposix_musl_math
	]
)
