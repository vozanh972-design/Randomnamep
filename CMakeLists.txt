cmake_minimum_required(VERSION 3.21)
project(LunexReDown VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

# Allow: cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg SvgWidgets)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/ui/TitleBar.cpp
    src/ui/ActivationWindow.cpp
    src/ui/MainWindow.cpp
    src/services/LicenseService.cpp
    src/services/DownloadService.cpp
    src/services/AppConfig.cpp
)

set(HEADERS
    src/ui/TitleBar.h
    src/ui/ActivationWindow.h
    src/ui/MainWindow.h
    src/services/LicenseService.h
    src/services/DownloadService.h
    src/services/AppConfig.h
    src/models/LicenseInfo.h
)

qt_add_executable(LunexReDown
    ${SOURCES}
    ${HEADERS}
    resources/resources.qrc
)

target_include_directories(LunexReDown PRIVATE
    src
    src/ui
    src/services
    src/models
)

target_link_libraries(LunexReDown PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::SvgWidgets
)

if(WIN32)
    set_target_properties(LunexReDown PROPERTIES
        OUTPUT_NAME "LunexReDown"
    )
endif()

set_target_properties(LunexReDown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Optional: for windeployqt convenience when packaging (not required for build)
