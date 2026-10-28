# Отдельный проект для сборки MineSweeper_Infinity со статическим Qt.
#
# Обычная сборка (динамический Qt) описана в ../CMakeLists.txt и этим файлом
# не затрагивается — оба проекта независимы и используют один и тот же код.
#
# Отличия от основной сборки:
#   * нет LinguistTools — статический Qt собирается из одного qtbase;
#   * плагины (платформа, стиль) вкомпилируются в exe, а не грузятся из DLL;
#   * рантайм MinGW линкуется статически, чтобы exe был самодостаточным.
#
# Настройка и сборка:
#   cmake -S static -B <build> -G Ninja \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=<путь к статическому Qt>
# или просто: scripts/build-static.sh

cmake_minimum_required(VERSION 3.16)

project(MineSweeper_Infinity VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------
# Конфигурация профилирования (Profile)
# ---------------------------------------------
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile" CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_PROFILE "-O2 -g" CACHE STRING "Flags for Profile build")
set(CMAKE_C_FLAGS_PROFILE   "-O2 -g" CACHE STRING "Flags for Profile build")
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "" CACHE STRING "Linker flags for Profile build")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Core Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Core Gui)

# Статический проект заточен под Windows: MinGW, вкомпилированные плагины
# платформы, проверка exe через objdump. На Linux всё это не нужно — там
# хватает обычной сборки из корневого CMakeLists.txt.
if(NOT WIN32)
    message(FATAL_ERROR
        "Проект static/ собирается только под Windows.\n"
        "На Linux используйте корневой CMakeLists.txt:\n"
        "  cmake -S . -B build && cmake --build build")
endif()

# Собирать статически имеет смысл только со статическим Qt: с динамическим
# этот проект даст тот же результат, что и основной, но без переводов.
get_target_property(QT_CORE_LIBRARY_TYPE Qt${QT_VERSION_MAJOR}::Core TYPE)

if(NOT QT_CORE_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR
        "Найден динамический Qt (${Qt${QT_VERSION_MAJOR}_DIR}).\n"
        "Этот проект только для статической сборки — укажите статический Qt:\n"
        "  cmake -S static -B <build> -DCMAKE_PREFIX_PATH=<статический Qt>\n"
        "Собрать статический Qt можно скриптом scripts/build-static.sh --build-qt")
endif()

set(MSI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(PROJECT_SOURCES
        ${MSI_SRC}/main.cpp

        ${MSI_SRC}/mainwindow.cpp
        ${MSI_SRC}/mainwindow.h
        ${MSI_SRC}/mainwindow.ui

        ${MSI_SRC}/settingspanel.cpp
        ${MSI_SRC}/settingspanel.h
        ${MSI_SRC}/settingspanel.ui

        ${MSI_SRC}/counterwidget.cpp
        ${MSI_SRC}/counterwidget.h

        ${MSI_SRC}/minefieldwidget.cpp
        ${MSI_SRC}/minefieldwidget.h
        ${MSI_SRC}/fogofwar.cpp
        ${MSI_SRC}/fogofwar.h

        ${MSI_SRC}/world.cpp
        ${MSI_SRC}/world.h
        ${MSI_SRC}/worldmanager.cpp
        ${MSI_SRC}/worldmanager.h
        ${MSI_SRC}/chunk.h
        ${MSI_SRC}/region.cpp
        ${MSI_SRC}/region.h

        ${MSI_SRC}/loader.cpp
        ${MSI_SRC}/loader.h
        ${MSI_SRC}/saver.cpp
        ${MSI_SRC}/saver.h

        ${MSI_SRC}/solver.cpp
        ${MSI_SRC}/solver.h
        ${MSI_SRC}/scoring.cpp
        ${MSI_SRC}/scoring.h

        ${MSI_SRC}/settings.cpp
        ${MSI_SRC}/settings.h
        ${MSI_SRC}/theme.cpp
        ${MSI_SRC}/theme.h

        ${MSI_SRC}/resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(MineSweeper_Infinity
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(MineSweeper_Infinity
        ${PROJECT_SOURCES}
    )
endif()

target_link_libraries(MineSweeper_Infinity PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
)

# ---------------------------------------------
# Плагины и рантайм статической сборки
# ---------------------------------------------
# У статического Qt плагины не подгружаются из DLL, а вкомпилируются в exe,
# поэтому их нужно подключить явно.
foreach(_qt_plugin IN ITEMS
        Qt${QT_VERSION_MAJOR}::QWindowsIntegrationPlugin
        Qt${QT_VERSION_MAJOR}::QModernWindowsStylePlugin)
    if(TARGET ${_qt_plugin})
        # Платформенный плагин обязателен: без него QApplication не создастся.
        # Плагин стиля даёт системный вид окна вместо Fusion.
        target_link_libraries(MineSweeper_Infinity PRIVATE ${_qt_plugin})
        message(STATUS "Подключён плагин ${_qt_plugin}")
    else()
        message(WARNING "В этой сборке Qt нет плагина ${_qt_plugin}")
    endif()
endforeach()

if(MINGW)
    # Без этого рядом с exe всё равно потребуются libstdc++-6.dll,
    # libgcc_s_seh-1.dll и libwinpthread-1.dll.
    target_link_options(MineSweeper_Infinity PRIVATE -static)
endif()

set_target_properties(MineSweeper_Infinity PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(MineSweeper_Infinity)
endif()
