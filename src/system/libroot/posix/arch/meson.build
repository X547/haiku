
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/arch/x86_64/posix_arch_x86_64.o',
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
		'headers/private',
		'headers/private/system',
		'headers/private/system/arch/x86_64',
	],
	include_directories: [
		'x86_64',
	],
)
