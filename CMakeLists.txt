cmake_minimum_required(VERSION 3.16)

project(QuickFilter VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

set(APP_FILES
    main.cpp
)

set(DRIVER_FILES
    driver/PeaceDriverAPI.h
    driver/peacedriverload.cpp
    driver/peacedriverload.h
    driver/peacesys.h
)

set(HELPER_FILES
    helper/Utils.cpp
    helper/Utils.h
)

set(VMP_FILES
    vmp/VMProtectSDK.h
)

set(HOOK_FILES
    src/hook/bytecode.h
    src/hook/cpu.h
    src/hook/filterhost.cpp
    src/hook/filterhost.h
    src/hook/format.h
    src/hook/hookcontrol.cpp
    src/hook/hookcontrol.h
    src/hook/memprotect.h
)

set(FILTER_FILES
    src/windowidfilter.cpp
    src/windowidfilter.h
    src/packetfilter.cpp
    src/packetfilter.h
)

set(QML_FILES
    Main.qml
    NeonButton.qml
    WindowIdFilter.qml
    PacketFilter.qml
)

qt_add_executable(appQuickFilter
    ${APP_FILES}
    ${DRIVER_FILES}
    ${HELPER_FILES}
    ${VMP_FILES}
)

qt_add_qml_module(appQuickFilter
    URI QuickFilter
    VERSION 1.0
    QML_FILES ${QML_FILES}
    SOURCES ${FILTER_FILES} ${HOOK_FILES}
)

source_group("app" FILES ${APP_FILES})
source_group("src" FILES ${FILTER_FILES})
source_group("src\\hook" FILES ${HOOK_FILES})
source_group("driver" FILES ${DRIVER_FILES})
source_group("helper" FILES ${HELPER_FILES})
source_group("vmp" FILES ${VMP_FILES})
source_group("qml" FILES ${QML_FILES})

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appQuickFilter PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appQuickFilter
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appQuickFilter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# 不用 $<CONFIG:...:D:/...>：生成器表达式用冒号分段，盘符会被截断成 /library/...
target_link_libraries(appQuickFilter
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        debug "${CMAKE_CURRENT_SOURCE_DIR}/library/peace/PeaceDriverAPI_d.lib"
        optimized "${CMAKE_CURRENT_SOURCE_DIR}/library/peace/PeaceDriverAPI.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/library/vmp/VMProtectSDK64.lib"
)

if (MSVC)
    target_compile_options(appQuickFilter PRIVATE /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS appQuickFilter
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 每次链接完成后把 Qt 运行库、插件和 QML 模块复制到 exe 同目录
if (WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" REQUIRED)

    add_custom_command(TARGET appQuickFilter POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
            --compiler-runtime
            "$<TARGET_FILE:appQuickFilter>"
        COMMENT "windeployqt: 打包 Qt 依赖"
        VERBATIM
    )
endif()
