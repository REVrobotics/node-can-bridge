cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

set (CMAKE_CXX_STANDARD 20)

project(node_canbridge)

# Configure MSVC to build using static libraries
# TODO: Determine if this is needed to stop requiring the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

if(NOT CMAKE_JS_INC)
    include("cmake-js.cmake")
    setup_cmakejs()
endif ()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/node_modules)
    message(FATAL_ERROR "You must run 'npm install' in order for NAPI to be available")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
    # Generate node.lib
    execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

# Set some things up for cross-compiling on Linux using clang
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(SYSTEM STREQUAL "LinuxX64")
        add_compile_options("-target" "x86_64-pc-linux-gnu")
    elseif(SYSTEM STREQUAL "LinuxArm64")
        add_compile_options("-target" "aarch64-pc-linux-gnu")
        add_link_options("-target" "aarch64-pc-linux-gnu" "-fuse-ld=/usr/bin/aarch64-linux-gnu-ld")
    endif()
endif()

add_definitions(-DNAPI_VERSION=9) # Keep in sync with binding-options.cjs, package.json binary.napi_version, and package.json prepublishOnly
add_definitions(-DNAPI_DISABLE_CPP_EXCEPTIONS)

set(SOURCES
        src/addon.cc
        src/canWrapper.cc
)

# Include the node-addon-api wrapper for Node-API
# TODO: Figure out if this todo applies. I honestly copied it from an older project
# TODO: Figure out why the build breaks without this, given that the CMake.js README
#       says that it will "add [node-addon-api] to the include search path automatically"
#       https://github.com/cmake-js/cmake-js/tree/6a2a50ba3d2e82a0ea80a8bb77cd2d3a03fb838c#node-api-and-node-addon-api
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
)

string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

include_directories(${NODE_ADDON_API_DIR} ${CMAKE_JS_INC} ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/externalCompileTimeDeps/include)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/externalCompileTimeDeps)

ADD_LIBRARY(CANBridge STATIC IMPORTED)
SET_TARGET_PROPERTIES(CANBridge PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/externalCompileTimeDeps/CANBridge.lib)

# target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} CANBridge wpiHal wpiutil)
# target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} ${CMAKE_CURRENT_SOURCE_DIR}/externalCompileTimeDeps/CANBridge.lib)
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} CANBridge)
