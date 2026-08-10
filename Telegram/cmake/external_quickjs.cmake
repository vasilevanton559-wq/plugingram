# Vendored QuickJS-NG (no libc — sandbox; no file/network from the engine).
get_filename_component(quickjs_loc "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/quickjs" REALPATH)

add_library(external_quickjs STATIC)
init_target(external_quickjs)
add_library(desktop-app::external_quickjs ALIAS external_quickjs)

set_target_properties(external_quickjs PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
)

nice_target_sources(external_quickjs ${quickjs_loc}
PRIVATE
	cutils.c
	libbf.c
	libregexp.c
	libunicode.c
	quickjs.c
)

target_include_directories(external_quickjs
PUBLIC
	${quickjs_loc}
)

target_compile_definitions(external_quickjs
PRIVATE
	_GNU_SOURCE
	WIN32_LEAN_AND_MEAN
	_WIN32_WINNT=0x0601
)

if (MSVC)
	target_compile_options(external_quickjs PRIVATE
		/wd4018
		/wd4100
		/wd4200
		/wd4244
		/wd4267
		/wd4996
	)
endif()
