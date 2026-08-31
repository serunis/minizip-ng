file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")
file(RENAME "${TEST_DIR}/random.bin" "${TEST_DIR}/input.bin")

set(collision_path "${TEST_DIR}/archive.zip.mz_tmp.0000")
set(collision_contents "existing collision file")
file(WRITE "${collision_path}" "${collision_contents}")

execute_process(
    COMMAND "${MINIZIP}" -0 archive.zip input.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE result
    TIMEOUT 10)

if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Temporary collision retry failed: ${result}")
endif()

file(READ "${collision_path}" collision_contents_after)
if(NOT collision_contents_after STREQUAL collision_contents)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Existing temporary collision file was changed")
endif()

file(GLOB temp_archives "${TEST_DIR}/archive.zip.mz_tmp.*")
list(LENGTH temp_archives temp_count)
if(NOT temp_count EQUAL 1 OR NOT temp_archives STREQUAL collision_path)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Unexpected temporary files after collision retry: ${temp_archives}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -x -d extracted archive.zip
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE extract_result
    TIMEOUT 10)

if(NOT extract_result EQUAL 0 OR NOT EXISTS "${TEST_DIR}/extracted/input.bin")
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Archive created after collision could not be verified")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
