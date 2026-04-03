libapp_server_decorator = static_library('app_server_decorator',
	'DecorManager.cpp',
	'Decorator.cpp',
	'DefaultDecorator.cpp',
	'DefaultWindowBehaviour.cpp',
	'MagneticBorder.cpp',
	'TabDecorator.cpp',
	'WindowBehaviour.cpp',
	cpp_args: [
		'-iquote', meson.current_source_dir(), '..',
	],
	gnu_symbol_visibility: 'hidden'
)
