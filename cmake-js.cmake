# This exists for pure-CMake builds, mainly useful for IDEs.
function(setup_cmakejs)
    find_program(CMAKEJS "cmake-js")
    find_program(NPM "npm")
    # first, check if we have NPM:
    if(NPM)
        message(VERBOSE "NPM found.")
    else()
        message(FATAL_ERROR "NPM not found. This project requires Node.js")
    endif()

    if(CMAKEJS)
        message(VERBOSE "CMake.js found.")
    else()
        message(ERROR "CMake.js not found, installing globally...")
        execute_process(COMMAND ${NPM_COMMAND} install -g cmake-js
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE NPM_OUTPUT)
        message(STATUS "CMake.js should now be installed.")
        message(VERBOSE ${NPM_OUTPUT})
    endif()


    if(WIN32)
        set(NPM_COMMAND ${NPM}.cmd)
        set(CMAKEJS_CMD ${CMAKEJS}.cmd)
    else()
        set(NPM_COMMAND ${NPM})
        set(CMAKEJS_CMD ${CMAKEJS})
    endif()

    function(GET_VARIABLE INPUT_STRING VARIABLE_TO_SELECT OUTPUT_VARIABLE)
        set(SEARCH_STRING "${VARIABLE_TO_SELECT}=\"")
        string(LENGTH "${SEARCH_STRING}" SEARCH_STRING_LENGTH)
        string(LENGTH "${INPUT_STRING}" INPUT_STRING_LENGTH)

        string(REPLACE "\\\\" "\\" INPUT_STRING "${INPUT_STRING}")

        string(FIND "${INPUT_STRING}" "${VARIABLE_TO_SELECT}=\"" SEARCH_STRING_INDEX)

        if(SEARCH_STRING_INDEX EQUAL -1)
            string(FIND "${INPUT_STRING}" "${VARIABLE_TO_SELECT}=" SEARCH_STRING_INDEX)

            math(EXPR SEARCH_STRING_INDEX "${SEARCH_STRING_INDEX}+${SEARCH_STRING_LENGTH}-1")

            string(SUBSTRING "${INPUT_STRING}" ${SEARCH_STRING_INDEX} ${INPUT_STRING_LENGTH} AFTER_SEARCH_STRING)
            string(FIND "${AFTER_SEARCH_STRING}" "'" QUOTE_INDEX)
            string(SUBSTRING "${AFTER_SEARCH_STRING}" "0" "${QUOTE_INDEX}" RESULT_STRING)
            set("${OUTPUT_VARIABLE}" "${RESULT_STRING}" PARENT_SCOPE)

        else()
            math(EXPR SEARCH_STRING_INDEX "${SEARCH_STRING_INDEX}+${SEARCH_STRING_LENGTH}")

            string(SUBSTRING "${INPUT_STRING}" ${SEARCH_STRING_INDEX} ${INPUT_STRING_LENGTH} AFTER_SEARCH_STRING)
            string(FIND "${AFTER_SEARCH_STRING}" "\"" QUOTE_INDEX)
            string(SUBSTRING "${AFTER_SEARCH_STRING}" "0" "${QUOTE_INDEX}" RESULT_STRING)
            set("${OUTPUT_VARIABLE}" "${RESULT_STRING}" PARENT_SCOPE)

        endif()
    endfunction(GET_VARIABLE)

    string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)
    if (CMAKE_BUILD_TYPE_LOWER STREQUAL "debug")
        execute_process(COMMAND "${CMAKEJS_CMD}" print-configure --debug
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE CMAKE_JS_OUTPUT
        )
    else()
        execute_process(COMMAND "${CMAKEJS_CMD}" print-configure
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE CMAKE_JS_OUTPUT
        )
    endif ()

    message(VERBOSE ${CMAKE_JS_OUTPUT})

    get_variable("${CMAKE_JS_OUTPUT}" "CMAKE_JS_INC" CMAKE_JS_INC)
    set(CMAKE_JS_INC "${CMAKE_JS_INC}" PARENT_SCOPE)

    get_variable("${CMAKE_JS_OUTPUT}" "CMAKE_JS_LIB" CMAKE_JS_LIB)
    set(CMAKE_JS_LIB "${CMAKE_JS_LIB}" PARENT_SCOPE)

    get_variable("${CMAKE_JS_OUTPUT}" "CMAKE_LIBRARY_OUTPUT_DIRECTORY" CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" PARENT_SCOPE)

    message(STATUS "[CMakeJS] Set up variables.")
endfunction(setup_cmakejs)
