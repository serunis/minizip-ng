execute_process(COMMAND id -u OUTPUT_VARIABLE user_id OUTPUT_STRIP_TRAILING_WHITESPACE)
if(user_id STREQUAL "0")
    return()
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${SOURCE_FILE}" "${TEST_DIR}/input.bin" COPYONLY)
configure_file("${SOURCE_FILE}" "${TEST_DIR}/archive*" COPYONLY)

execute_process(
    COMMAND "${MINIZIP}" -0 -o archive.zip input.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE create_result
    TIMEOUT 10)
if(NOT create_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Initial archive creation failed: ${create_result}")
endif()

execute_process(COMMAND chmod 0500 "${TEST_DIR}" RESULT_VARIABLE chmod_result)
if(NOT chmod_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Could not make the test directory read-only")
endif()

# An explicit input outside the output path must write directly to the existing
# archive. Creating a sibling temporary file is impossible in this directory.
execute_process(
    COMMAND "${MINIZIP}" -0 -o archive.zip input.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE direct_result
    TIMEOUT 10)

execute_process(COMMAND chmod 0700 "${TEST_DIR}")
if(NOT direct_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Non-overlapping output unexpectedly required a temporary file: ${direct_result}")
endif()

execute_process(COMMAND chmod 0500 "${TEST_DIR}")

# An existing path containing '*' is a literal input, not a wildcard. Although
# archive.zip matches that text as a pattern, it must still write directly.
execute_process(
    COMMAND "${MINIZIP}" -0 -o archive.zip "archive*"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE literal_wildcard_result
    TIMEOUT 10)

execute_process(COMMAND chmod 0700 "${TEST_DIR}")
if(NOT literal_wildcard_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Literal '*' input unexpectedly required a temporary file: ${literal_wildcard_result}")
endif()

execute_process(COMMAND chmod 0500 "${TEST_DIR}")

# The wildcard selects archive.zip, so this operation must attempt a temporary
# output and fail instead of updating the archive directly.
execute_process(
    COMMAND "${MINIZIP}" -0 -o archive.zip "./*"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE overlap_result
    TIMEOUT 10)

execute_process(COMMAND chmod 0700 "${TEST_DIR}")
if(overlap_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Overlapping output did not use a temporary file")
endif()

file(GLOB temp_archives "${TEST_DIR}/archive.zip.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Failed temporary creation left files behind: ${temp_archives}")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
