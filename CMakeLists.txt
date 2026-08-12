cmake_minimum_required(VERSION 3.19)
project(YtArchive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Sql Network)

# Point this at your own repository before publishing a release.
set(YTA_GITHUB_REPO "your-github-user/ytarchive"
    CACHE STRING "GitHub 'owner/repo' polled for update checks")

qt_standard_project_setup()

qt_add_executable(ytarchive
    src/main.cpp
    src/Models.h            src/Models.cpp
    src/Settings.h            src/Settings.cpp
    src/FileTime.h            src/FileTime.cpp
    src/Database.h            src/Database.cpp
    src/YtDlp.h               src/YtDlp.cpp
    src/ChannelSync.h         src/ChannelSync.cpp
    src/DownloadManager.h     src/DownloadManager.cpp
    src/ThumbnailCache.h      src/ThumbnailCache.cpp
    src/VideoModel.h          src/VideoModel.cpp
    src/VideoCardDelegate.h   src/VideoCardDelegate.cpp
    src/DownloadsPanel.h      src/DownloadsPanel.cpp
    src/PreferencesDialog.h   src/PreferencesDialog.cpp
    src/UpdateChecker.h       src/UpdateChecker.cpp
    src/MainWindow.h          src/MainWindow.cpp
)

qt_add_resources(ytarchive "appresources"
    PREFIX "/"
    FILES resources/style.qss
)

target_compile_definitions(ytarchive PRIVATE
    YTA_VERSION="${PROJECT_VERSION}"
    YTA_GITHUB_REPO="${YTA_GITHUB_REPO}")

target_link_libraries(ytarchive PRIVATE Qt6::Widgets Qt6::Sql Qt6::Network)

set_target_properties(ytarchive PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

install(TARGETS ytarchive
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)