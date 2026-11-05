cmake_minimum_required(VERSION 3.16)
project(lxboot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

add_library(lxboot_core STATIC
    src/core/Store.cpp
    src/core/Host.cpp
    src/core/Metrics.cpp
    src/core/ConfigGen.cpp
    src/core/Engine.cpp
    src/core/Ipc.cpp
)
target_include_directories(lxboot_core PUBLIC src)
target_link_libraries(lxboot_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network)

add_executable(lxbootd src/app/main_daemon.cpp)
target_link_libraries(lxbootd PRIVATE lxboot_core Qt6::Core)

add_executable(lxboot src/app/main_cli.cpp)
target_link_libraries(lxboot PRIVATE lxboot_core Qt6::Core)

add_executable(lxboot-ui
    src/app/main_ui.cpp
    src/ui/MainWindow.cpp
    src/ui/Pages.cpp
)
target_link_libraries(lxboot-ui PRIVATE lxboot_core Qt6::Core Qt6::Gui Qt6::Widgets)

install(TARGETS lxbootd lxboot lxboot-ui RUNTIME DESTINATION bin)
install(DIRECTORY share/lxboot DESTINATION share)
install(FILES deploy/lxbootd.service deploy/lxboot-ui.service deploy/lxboot-dnsmasq.service DESTINATION lib/systemd/system)
install(PROGRAMS deploy/lxboot-dhcp-hook DESTINATION lib/lxboot)
