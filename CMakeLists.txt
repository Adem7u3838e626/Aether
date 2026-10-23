cmake_minimum_required(VERSION 3.20)
project(aether VERSION 0.6.1 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Svg Network Concurrent)
find_package(KF6WindowSystem QUIET)

# Keep Aether's compiled QML resources under :/Aether, where main.cpp and the
# installed qmldir expect them, while explicitly opting into Qt's QML policy.
qt_policy(SET QTP0001 NEW)

# QTP0004 was added in Qt 6.8. Enable generated qmldir files for QML
# subdirectories when the installed Qt supports the policy.
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.8")
    qt_policy(SET QTP0004 NEW)
endif()

add_subdirectory(src)

# Install themes
install(DIRECTORY themes/ DESTINATION share/Aether/themes FILES_MATCHING PATTERN "*.toml")

# Install the generated Aether QML module: qmldir + qml/ tree + qmltypes.
# The qmldir keeps Qt's `prefer :/Aether/`, so at runtime the module resolves
# to the qmlcachegen-compiled qrc payload and nothing is recompiled on launch
# (a random AppImage mount path defeats the on-disk .qmlc cache every start).
# The on-disk tree is only the fallback main.cpp uses when the qrc load fails,
# as it did with Qt 6.7.3 NO_CACHEGEN in v0.4.14 ("SettingsPanel is not a
# type"); release builds force AETHER_ENABLE_QML_CACHEGEN=ON for that reason.
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/Aether/
    DESTINATION share/Aether/Aether
    FILES_MATCHING
        PATTERN "*.qml"
        PATTERN "qmldir"
        PATTERN "*.qmltypes")


option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
