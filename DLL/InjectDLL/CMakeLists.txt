cmake_minimum_required(VERSION 3.20)
project(MilkBarClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(MilkBarClient SHARED
    BombSyncer.cpp
    CharacterClasses.cpp
    Client.cpp
    Deserializer.cpp
    dllmain.cpp
    dllmain_Functions.cpp
    InterpolationFunctions.cpp
    Interpretation.cpp
    LoggerService.cpp
    MessagerService.cpp
    multiplayerQuest.cpp
    NamedPipes.cpp
    Player.cpp
    QuaternionBE.cpp
    QuestSync.cpp
    ReadCemu.cpp
    Scanner.cpp
    Serialization.cpp
    Serializer.cpp
    Spawning.cpp
    SpawningFunction.cpp
    Vec3fBE.cpp
    WorldClass.cpp
)

target_include_directories(MilkBarClient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(WIN32)
    target_link_libraries(MilkBarClient PRIVATE ws2_32)
    set_target_properties(MilkBarClient PROPERTIES OUTPUT_NAME InjectDLL)
elseif(ANDROID)
    # Android provides pthread APIs in libc; there is no separate libpthread.
    target_link_libraries(MilkBarClient PRIVATE dl log)
    target_link_options(MilkBarClient PRIVATE "-Wl,-z,max-page-size=16384")
    set_target_properties(MilkBarClient PROPERTIES OUTPUT_NAME MilkBarClient)
elseif(APPLE)
    target_link_libraries(MilkBarClient PRIVATE "-framework CoreFoundation")
    set_target_properties(MilkBarClient PROPERTIES
        OUTPUT_NAME MilkBarClient
        INSTALL_RPATH "@loader_path")
else()
    target_link_libraries(MilkBarClient PRIVATE dl pthread)
    set_target_properties(MilkBarClient PROPERTIES OUTPUT_NAME MilkBarClient)
endif()

install(TARGETS MilkBarClient
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
