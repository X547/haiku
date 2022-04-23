libos_arch_x86_64 = static_library(
	'os_arch_x86_64',
	[
		'generic/generic_atomic.cpp',
		'generic/generic_stack_trace.cpp',
		'x86_64/byteorder.S',
		'x86_64/get_stack_frame.S',
		'x86_64/system_info.cpp',
		'x86_64/system_time.cpp',
		'x86_64/thread.cpp',
		'x86_64/time.cpp',
		'x86_64/tls.cpp',
	],
	c_args: [
		'-D_ASSEMBLER',
	],
	include_directories: [
		hdr_private,
		hdr_private_kernel,
		hdr_private_libroot,
		hdr_private_shared,
		hdr_private_system,
		include_directories(
			'generic',
			'x86_64',
			is_system: false
		),
	],
)
