cmake_minimum_required(VERSION 3.20)
project(SliceChaser VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Slice Chaser — FFGL effect plugin for Resolume Arena / Avenue 7.x
#
#   Windows : cmake -B build -A x64     && cmake --build build --config Release
#   macOS   : cmake -B build -G Xcode   && cmake --build build --config Release
#
# Result: build/dist/SliceChaser.dll (Windows) or build/dist/SliceChaser.bundle (macOS).
# No runtime dependencies. FFGL SDK and GLEW are downloaded automatically.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /bigobj)
endif()

include(FetchContent)

set(FFGL_SDK_DIR "" CACHE PATH "Path to a checkout of github.com/resolume/ffgl (downloaded if empty)")
if(NOT FFGL_SDK_DIR)
    FetchContent_Declare(ffgl GIT_REPOSITORY https://github.com/resolume/ffgl.git GIT_TAG master GIT_SHALLOW TRUE
        SOURCE_SUBDIR _populate_only_)
    FetchContent_MakeAvailable(ffgl)
    set(FFGL_SDK_DIR ${ffgl_SOURCE_DIR})
endif()
set(FFGL_LIB ${FFGL_SDK_DIR}/source/lib)

if(NOT APPLE)
    set(GLEW_DIR "" CACHE PATH "Path to an extracted glew-2.2.0 source release (downloaded if empty)")
    if(NOT GLEW_DIR)
        FetchContent_Declare(glew URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR _populate_only_)
        FetchContent_MakeAvailable(glew)
        set(GLEW_DIR ${glew_SOURCE_DIR})
    endif()
endif()

# Sources may live in src/ (normal layout) or directly next to this file
# (when uploaded through a browser that flattens folders).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/FFGLChaser.cpp)
    set(SRC src)
else()
    set(SRC .)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/chaser_tests.cpp)
    set(TESTS_DIR tests)
else()
    set(TESTS_DIR .)
endif()
message(STATUS "Slice Chaser sources: ${SRC}")

set(SOURCES
    ${SRC}/FFGLChaser.cpp
    ${SRC}/FFGLChaser.h
    ${SRC}/ChaseEngine.cpp
    ${SRC}/ChaseEngine.h
    ${SRC}/Zones.cpp
    ${SRC}/Zones.h
    ${SRC}/MiniXml.cpp
    ${SRC}/MiniXml.h
    ${FFGL_LIB}/FFGLSDK.cpp
)
if(NOT APPLE)
    list(APPEND SOURCES ${GLEW_DIR}/src/glew.c)
endif()
if(WIN32)
    file(WRITE ${CMAKE_BINARY_DIR}/SliceChaser.def "LIBRARY SliceChaser\nEXPORTS\n\tplugMain\n")
    list(APPEND SOURCES ${CMAKE_BINARY_DIR}/SliceChaser.def)
endif()

if(APPLE)
    add_library(SliceChaser MODULE ${SOURCES})
    set_target_properties(SliceChaser PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME SliceChaser
        MACOSX_BUNDLE_GUI_IDENTIFIER com.slicechaser.ffgl
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
    target_link_libraries(SliceChaser PRIVATE "-framework OpenGL" "-framework Cocoa")
else()
    add_library(SliceChaser SHARED ${SOURCES})
    set_target_properties(SliceChaser PROPERTIES PREFIX "")
    target_compile_definitions(SliceChaser PRIVATE GLEW_STATIC GLEW_NO_GLU)
    target_include_directories(SliceChaser PRIVATE ${GLEW_DIR}/include)
endif()
target_include_directories(SliceChaser PRIVATE ${FFGL_LIB} ${SRC})

if(WIN32)
    target_link_libraries(SliceChaser PRIVATE opengl32)
    target_compile_definitions(SliceChaser PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(MINGW)
        target_link_options(SliceChaser PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(NOT APPLE)
    # Linux: test builds only (Resolume has no Linux version)
    target_link_libraries(SliceChaser PRIVATE dl pthread)
endif()

set(DIST_DIR ${CMAKE_BINARY_DIR}/dist)
if(WIN32)
    add_custom_command(TARGET SliceChaser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SliceChaser> ${DIST_DIR}/SliceChaser.dll)
elseif(APPLE)
    add_custom_command(TARGET SliceChaser POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:SliceChaser>
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_BUNDLE_DIR:SliceChaser> ${DIST_DIR}/SliceChaser.bundle)
endif()

option(SLICECHASER_BUILD_TESTS "Build unit tests for zones and the chase engine" OFF)
if(SLICECHASER_BUILD_TESTS)
    add_executable(chaser_tests ${TESTS_DIR}/chaser_tests.cpp ${SRC}/ChaseEngine.cpp ${SRC}/Zones.cpp ${SRC}/MiniXml.cpp)
    target_include_directories(chaser_tests PRIVATE ${SRC})
endif()
