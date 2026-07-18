
find_package(Qt6 COMPONENTS Concurrent Network Svg Widgets REQUIRED)

set(CMAKE_AUTOUIC_SEARCH_PATHS forms)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

file(GLOB launcher_src
	"include/*.h"
	"src/*.cpp"
)

file(GLOB launcher_forms
	"forms/*.ui"
)

add_executable(launcher WIN32
	${launcher_src}
	${launcher_forms}
	"resources/launcher.qrc"
)


target_link_libraries(launcher Qt6::Concurrent Qt6::Network Qt6::Svg Qt6::Widgets)
if(WIN32)
target_link_libraries(launcher setupapi mfplay)
endif()
if (CLANG AND NOT PS5SIM_CLANG_CL)
target_link_libraries(launcher pthread)
endif()
target_include_directories(launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(launcher PROPERTIES AUTOUIC ON AUTOMOC ON AUTORCC ON)

# PS5 boot video played by the splash screen; shipped next to the exe (too big for qrc).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/boot_splash.mp4")
	add_custom_command(TARGET launcher POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/resources/boot_splash.mp4"
			$<TARGET_FILE_DIR:launcher>/boot_splash.mp4
	)
	install(FILES "resources/boot_splash.mp4" DESTINATION .)
endif()

# Home-screen chime played right after the boot video ends.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/boot_chime.mp3")
	add_custom_command(TARGET launcher POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/resources/boot_chime.mp3"
			$<TARGET_FILE_DIR:launcher>/boot_chime.mp3
	)
	install(FILES "resources/boot_chime.mp3" DESTINATION .)
endif()

if(WIN32 AND NOT PS5SIM_CLANG_CL)
	set_target_properties(launcher PROPERTIES LINK_FLAGS "${PS5SIM_LD_OPTIONS}")
endif()

add_dependencies(launcher Ps5SimGitVersion)  
add_dependencies(launcher ps5sim_emulator)

if(PS5SIM_CLANG_CL)
	add_custom_command(TARGET launcher POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PDB_FILE:ps5sim_emulator> $<TARGET_FILE_DIR:launcher>/$<TARGET_PDB_FILE_NAME:ps5sim_emulator>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE_DIR:ps5sim_emulator>/${PS5SIM_EMULATOR_MAP_NAME} $<TARGET_FILE_DIR:launcher>/${PS5SIM_EMULATOR_MAP_NAME}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PS5SIM_THIRD_PARTY_DIR}/winpthread/bin/libwinpthread-1.dll" $<TARGET_FILE_DIR:launcher>/libwinpthread-1.dll
	)
endif()

if(WIN32)
    find_program(QT_WINDEPLOYQT NAMES windeployqt PATHS "${Qt6_DIR}/../../../bin")
    if(NOT QT_WINDEPLOYQT)
    	message(FATAL_ERROR "Could not find windeployqt")
    endif()
	add_custom_command(TARGET launcher POST_BUILD
		COMMAND ${QT_WINDEPLOYQT} $<TARGET_FILE:launcher>
			--no-compiler-runtime
			--no-system-d3d-compiler
			--no-system-dxc-compiler
			--no-opengl-sw
			--no-translations
		COMMENT "Deploying Qt runtime and TLS plugins"
		VERBATIM
	)
endif()

set(launcher_name "launcher")

set_target_properties(launcher PROPERTIES OUTPUT_NAME ${launcher_name})

install(TARGETS launcher DESTINATION .)

if(WIN32)
    install(CODE "
    		execute_process(
    			COMMAND ${QT_WINDEPLOYQT} \"\${CMAKE_INSTALL_PREFIX}/${launcher_name}.exe\"
				--no-compiler-runtime
				--no-system-d3d-compiler
				--no-system-dxc-compiler
				--no-opengl-sw
				--no-translations
    		)
    ")
endif()


set(tidy_dirs "${CMAKE_SOURCE_DIR}/launcher/include") 
set(iwyu_maps "${CMAKE_SOURCE_DIR}/launcher/utils/qt6_16.imp")

get_property(inc_headers TARGET launcher PROPERTY INCLUDE_DIRECTORIES)

list(APPEND inc_headers
	${CMAKE_SOURCE_DIR}/launcher
	${CMAKE_BINARY_DIR}/launcher/launcher_autogen/include
	${Qt6Widgets_INCLUDE_DIRS}
	${Qt6Core_INCLUDE_DIRS}
	${Qt6Gui_INCLUDE_DIRS}
	#${Qt6_DIR}/../../../include
	#${Qt6_DIR}/../../../include/QtWidgets
	#${Qt6_DIR}/../../../include/QtCore
	#${Qt6_DIR}/../../../include/QtGui
)

include_what_you_use_with_mappings(launcher "${inc_headers}" "${iwyu_maps}")

clang_tidy_check(launcher "" "${tidy_dirs}" "${inc_headers}")
