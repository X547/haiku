hdr_linprog = include_directories('headers')

liblinprog = static_library(
	'linprog',
	[
		'src/ActiveSetSolver.cpp',
		'src/Constraint.cpp',
		'src/LayoutOptimizer.cpp',
		'src/LinearSpec.cpp',
		'src/Summand.cpp',
		'src/Variable.cpp',
	],
	include_directories: [
		hdr_private_shared,
		hdr_linprog,
	],
	gnu_symbol_visibility: 'hidden'
)

dep_linprog = declare_dependency(
	link_with: liblinprog,
	include_directories: hdr_linprog
)
