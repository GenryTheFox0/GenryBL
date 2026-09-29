cmake_minimum_required(VERSION 3.21)
project(GenryBL VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Concurrent Network)
qt_standard_project_setup(REQUIRES 6.8)

# GB_RELEASE=ON: the build for people (GitHub): the same program, only its own lists go to work/ and no edition badge;
# OFF = a developer build: its «Удалённые» / face fixes go straight into data/ (they ship with the program).
option(GB_RELEASE "Public release build" OFF)
if(GB_RELEASE)
    add_compile_definitions(GB_RELEASE=1)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
    # a PDB for the release too: the crash catcher (src/app/CrashCatcher.cpp) turns addresses into GenryBL's own
    # function names and lines; /OPT:REF/ICF keep the exe as lean as without it
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# ---- core: compiler, ES assets, preview, builder (no UI) ----
add_library(gb_core STATIC
    src/core/Text.cpp
    src/core/Py.cpp
    src/core/Pickle.cpp
    src/core/Rpa.cpp
    src/core/Compiler.cpp
    src/core/EsAssets.cpp
    src/core/Scene.cpp
    src/core/Renderer.cpp
    src/core/Builder.cpp
    src/core/Lint.cpp
    src/core/Forms.cpp
    src/core/Weather.cpp
    src/core/Library.cpp
    src/core/PhoneAssets.cpp
    src/core/Screenplay.cpp
    src/core/Overlays.cpp
    src/core/Wardrobe.cpp
    src/core/Cinema.cpp
)
target_include_directories(gb_core PUBLIC src/core)
target_link_libraries(gb_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent PRIVATE Qt6::CorePrivate)

qt_add_executable(gb_selftest tools/selftest.cpp)
set_target_properties(gb_selftest PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(gb_selftest PRIVATE gb_core)
target_compile_definitions(gb_selftest PRIVATE GB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

qt_add_executable(gb_cli tools/gb_cli.cpp)
set_target_properties(gb_cli PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(gb_cli PRIVATE gb_core)
target_compile_definitions(gb_cli PRIVATE GB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---- the app: C++ engine + QML UI ----
qt_add_executable(GenryBL WIN32 src/app/main.cpp src/app.rc)
set_source_files_properties(qml/Theme.qml qml/Sfx.qml qml/Music.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(GenryBL
    URI GenryBL
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Sfx.qml
        qml/Music.qml
        qml/InkText.qml
        qml/PillButton.qml
        qml/DarkTab.qml
        qml/DarkCombo.qml
        qml/SmoothImage.qml
        qml/Toasts.qml
        qml/MainMenuScreen.qml
        qml/AlbumScreen.qml
        qml/SettingsScreen.qml
        qml/GalleryScreen.qml
        qml/Launcher.qml
        qml/DialogueDialog.qml
        qml/ImagePicker.qml
        qml/ItemPicker.qml
        qml/FieldInput.qml
        qml/CommandForm.qml
        qml/MapEditor.qml
        qml/ScreenplayDialog.qml
        qml/LibraryBrowser.qml
        qml/ChoiceDialog.qml
        qml/ModTitleDialog.qml
        qml/ExportDialog.qml
        qml/Editor.qml
        qml/CodeEditor.qml
        qml/Palette.qml
        qml/CharacterBrowser.qml
        qml/BackgroundBrowser.qml
        qml/AudioBrowser.qml
        qml/PreviewPane.qml
        qml/CinemaView.qml
        qml/FaceFixDialog.qml
        qml/SummerBackdrop.qml
        qml/AgeGate.qml
        qml/SetupScreen.qml
        qml/InstallerView.qml
        qml/DarkCheck.qml
    SOURCES
        src/app/Engine.h src/app/Engine.cpp
        src/app/Images.h src/app/Images.cpp
        src/app/Highlighter.h src/app/Highlighter.cpp
        src/app/Installer.cpp
        src/app/Updater.cpp
        src/app/CrashCatcher.h src/app/CrashCatcher.cpp
)
target_include_directories(GenryBL PRIVATE src/app)
target_link_libraries(GenryBL PRIVATE gb_core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent Qt6::Network)
if(WIN32)
    target_link_libraries(GenryBL PRIVATE ole32 shell32 uuid dbghelp)
endif()

# ---- GenryBL_Setup.exe: the one-file installer (plain Win32, the zipped program is appended by make_release.py)
if(WIN32)
    add_executable(GenryBL_Setup WIN32 tools/setup_boot.cpp src/app.rc)
    target_link_libraries(GenryBL_Setup PRIVATE comctl32 shell32)
    # the static C runtime: the one file people download starts on a clean Windows (no VC++ Redistributable)
    set_property(TARGET GenryBL_Setup PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
