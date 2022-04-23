
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/crypt/posix_crypt.o',
	[
		'SHA256.cpp',
		'crypt.cpp',
		'crypto_scrypt.cpp',
		'crypto_scrypt_smix.cpp',
		'pbkdf2.cpp',
	],
	include_directories: [
		'headers/compatibility/bsd',
		'headers/compatibility/gnu',
		'headers/private',
		'headers/private/libroot',
		'headers/private/system',
		'headers/private/system/arch/x86_64',
	],
	include_directories: [
		'.',
	],
)
