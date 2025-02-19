
set(INCROOT ${PROJECT_SOURCE_DIR}/include/SFML/Audio)
set(SRCROOT ${PROJECT_SOURCE_DIR}/src/SFML/Audio)

# all source files
set(SRC
    ${SRCROOT}/ALCheck.cpp
    ${SRCROOT}/ALCheck.hpp
    ${SRCROOT}/AlResource.cpp
    ${INCROOT}/AlResource.hpp
    ${SRCROOT}/AudioDevice.cpp
    ${SRCROOT}/AudioDevice.hpp
    ${INCROOT}/Export.hpp
    ${SRCROOT}/Listener.cpp
    ${INCROOT}/Listener.hpp
    ${SRCROOT}/Music.cpp
    ${INCROOT}/Music.hpp
    ${SRCROOT}/Sound.cpp
    ${INCROOT}/Sound.hpp
    ${SRCROOT}/SoundBuffer.cpp
    ${INCROOT}/SoundBuffer.hpp
    ${SRCROOT}/SoundBufferRecorder.cpp
    ${INCROOT}/SoundBufferRecorder.hpp
    ${SRCROOT}/InputSoundFile.cpp
    ${INCROOT}/InputSoundFile.hpp
    ${SRCROOT}/OutputSoundFile.cpp
    ${INCROOT}/OutputSoundFile.hpp
    ${SRCROOT}/SoundRecorder.cpp
    ${INCROOT}/SoundRecorder.hpp
    ${SRCROOT}/SoundSource.cpp
    ${INCROOT}/SoundSource.hpp
    ${SRCROOT}/SoundStream.cpp
    ${INCROOT}/SoundStream.hpp
)
source_group("" FILES ${SRC})

set(CODECS_SRC
    ${SRCROOT}/SoundFileFactory.cpp
    ${INCROOT}/SoundFileFactory.hpp
    ${INCROOT}/SoundFileFactory.inl
    ${INCROOT}/SoundFileReader.hpp
    #${SRCROOT}/SoundFileReaderFlac.hpp
    #${SRCROOT}/SoundFileReaderFlac.cpp
    #${SRCROOT}/SoundFileReaderMp3.hpp
    #${SRCROOT}/SoundFileReaderMp3.cpp
    ${SRCROOT}/SoundFileReaderOgg.hpp
    ${SRCROOT}/SoundFileReaderOgg.cpp
    ${SRCROOT}/SoundFileReaderWav.hpp
    ${SRCROOT}/SoundFileReaderWav.cpp
    ${INCROOT}/SoundFileWriter.hpp

    ${SRCROOT}/SoundFileWriterOgg.hpp
    ${SRCROOT}/SoundFileWriterOgg.cpp
    ${SRCROOT}/SoundFileWriterWav.hpp
    ${SRCROOT}/SoundFileWriterWav.cpp
    )
if(NOT SFML_OS_EMSCRIPTEN)
    list(APPEND CODECS_SRC
        ${SRCROOT}/SoundFileReaderFlac.hpp
        ${SRCROOT}/SoundFileReaderFlac.cpp

        ${SRCROOT}/SoundFileWriterFlac.hpp
        ${SRCROOT}/SoundFileWriterFlac.cpp)
endif()

source_group("codecs" FILES ${CODECS_SRC})

# let CMake know about our additional audio libraries paths (on Windows and OSX)
if(SFML_OS_WINDOWS)
    list(APPEND CMAKE_INCLUDE_PATH "${PROJECT_SOURCE_DIR}/extlibs/headers/AL")
elseif(SFML_OS_MACOSX)
    list(APPEND CMAKE_LIBRARY_PATH "${PROJECT_SOURCE_DIR}/extlibs/libs-osx/Frameworks")
elseif(SFML_OS_ANDROID)
    list(APPEND CMAKE_INCLUDE_PATH "${PROJECT_SOURCE_DIR}/extlibs/headers/AL")
    list(APPEND CMAKE_INCLUDE_PATH "${PROJECT_SOURCE_DIR}/extlibs/android")
endif()

# find external libraries
sfml_find_package(OpenAL INCLUDE "OPENAL_INCLUDE_DIR" LINK "OPENAL_LIBRARY")
sfml_find_package(FLAC INCLUDE "FLAC_INCLUDE_DIR" LINK "FLAC_LIBRARY")

if(NOT SFML_OS_EMSCRIPTEN)
    sfml_find_package(VORBIS INCLUDE "VORBIS_INCLUDE_DIRS" LINK "VORBIS_LIBRARIES")

    # avoids warnings in vorbisfile.h
    target_compile_definitions(VORBIS INTERFACE "OV_EXCLUDE_STATIC_CALLBACKS")
    target_compile_definitions(FLAC INTERFACE "FLAC__NO_DLL")
endif()

# define the sfml-audio target
sfml_add_library(Audio
                 SOURCES ${SRC} ${CODECS_SRC})

# setup dependencies
target_link_libraries(sfml-audio PRIVATE OpenAL)
target_include_directories(sfml-audio PRIVATE ${OPENAL_INCLUDE_DIR})

# minimp3 sources
target_include_directories(sfml-audio SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/extlibs/headers/minimp3")

if(SFML_OS_ANDROID)
    target_link_libraries(sfml-audio PRIVATE android OpenSLES)
endif()

if(NOT SFML_OS_EMSCRIPTEN)
target_link_libraries(sfml-audio
                      PUBLIC SFML::System
                      PRIVATE VORBIS FLAC)
else()
    target_include_directories(sfml-audio PRIVATE "$ENV{EMSCRIPTEN}/system/include/AL")

    target_link_libraries(sfml-audio PUBLIC sfml-system)
    target_link_options(sfml-audio PUBLIC "SHELL:-s USE_VORBIS=1")
    target_compile_options(sfml-audio PUBLIC "SHELL:-s USE_VORBIS=1")
endif()
