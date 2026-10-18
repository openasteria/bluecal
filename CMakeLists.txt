cmake_minimum_required(VERSION 3.16)
project(bluecal VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(LIBNOTIFY REQUIRED libnotify)

add_executable(bluecal
    src/main.cpp
    src/storage.cpp
    src/notify.cpp
)

target_include_directories(bluecal PRIVATE
    include
    ${NCURSES_INCLUDE_DIRS}
    ${LIBNOTIFY_INCLUDE_DIRS}
)

target_link_libraries(bluecal PRIVATE
    ${NCURSES_LIBRARIES}
    ${LIBNOTIFY_LIBRARIES}
    pthread
)

target_compile_options(bluecal PRIVATE -Wall -Wextra)

install(TARGETS bluecal RUNTIME DESTINATION bin)
install(FILES scripts/bluecal-reminderd DESTINATION bin PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES scripts/bluecal.service DESTINATION lib/systemd/user)
