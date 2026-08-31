file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${SOURCE_FILE}" "${TEST_DIR}/first.bin" COPYONLY)
configure_file("${SECOND_FILE}" "${TEST_DIR}/second.bin" COPYONLY)

execute_process(
    COMMAND "${MINIZIP}" -0 -o base.zip first.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE create_result
    TIMEOUT 10)

if(NOT create_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Base archive creation failed: ${create_result}")
endif()

set(prefix "SFX-PREFIX\n")
file(WRITE "${TEST_DIR}/prefix.bin" "${prefix}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E cat prefix.bin base.zip
    WORKING_DIRECTORY "${TEST_DIR}"
    OUTPUT_FILE "${TEST_DIR}/archive.zip"
    RESULT_VARIABLE prefix_result
    TIMEOUT 10)

if(NOT prefix_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Prefixed archive creation failed: ${prefix_result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -0 -a archive.zip second.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE append_result
    TIMEOUT 10)

if(NOT append_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Prefixed archive append failed: ${append_result}")
endif()

string(LENGTH "${prefix}" prefix_length)
file(READ "${TEST_DIR}/archive.zip" prefix_after LIMIT ${prefix_length})
if(NOT prefix_after STREQUAL prefix)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Archive prefix was not preserved")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
