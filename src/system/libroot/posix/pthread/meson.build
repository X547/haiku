
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/pthread/posix_pthread.o',
	[
		'pthread.cpp',
		'pthread_atfork.c',
		'pthread_attr.c',
		'pthread_barrier.cpp',
		'pthread_cancel.cpp',
		'pthread_cleanup.cpp',
		'pthread_cond.cpp',
		'pthread_condattr.c',
		'pthread_key.cpp',
		'pthread_mutex.cpp',
		'pthread_mutexattr.c',
		'pthread_once.cpp',
		'pthread_rwlock.cpp',
		'pthread_spinlock.cpp',
	],
	include_directories: [
		'headers/private',
		'headers/private/kernel',
		'headers/private/libroot',
		'headers/private/shared',
		'headers/private/system',
		'headers/private/system/arch/x86_64',
	],
	include_directories: [
		'.',
	],
)
