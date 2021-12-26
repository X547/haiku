sources = [
	'VideoNode.cpp',
	'VideoProducer.cpp',
	'VideoConsumer.cpp',
	'CompositeProducer.cpp',
	'CompositeConsumer.cpp',
	'CompositeProxy.cpp',
	'RasBuf.cpp',
	'TestProducerBase.cpp',
	'BufferQueue.cpp',
]

libvideostreams = shared_library(
	'videostreams',
	sources,
	include_directories: [
		'/boot/system/develop/headers/private/app',
		'/boot/system/develop/headers/private/shared'
	],
	dependencies: [
		compiler.find_library('be')
	],
	gnu_symbol_visibility: 'hidden',
	install: true,
	install_dir: 'lib'
)
