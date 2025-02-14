libposix_arch_x86_64 = static_library(
	'posix_arch_x86_64',
	[
		'generic/longjmp_return.c',
		'generic/setjmp_save_sigs.c',
		'x86_64/fenv.c',
		'x86_64/siglongjmp.S',
		'x86_64/sigsetjmp.S',
	],
	cpp_args: [
		'_ASSEMBLER',
	],
	include_directories: [
		hdr_private_system,
		'x86_64',
	],
)
