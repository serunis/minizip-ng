file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")

execute_process(
    COMMAND "${MINIZIP}" -0 -o self.zip "./*"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE result
    TIMEOUT 10)

if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Self-inclusion command failed: ${result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -x -d extracted self.zip
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE extract_result
    TIMEOUT 10)

if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Self-inclusion archive extraction failed: ${extract_result}")
endif()

file(GLOB_RECURSE extracted_entries
    RELATIVE "${TEST_DIR}/extracted"
    LIST_DIRECTORIES false
    "${TEST_DIR}/extracted/*")
list(LENGTH extracted_entries entry_count)
if(NOT entry_count EQUAL 1 OR NOT extracted_entries STREQUAL "random.bin")
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Unexpected self-inclusion archive entries: ${extracted_entries}")
endif()

file(GLOB temp_archives "${TEST_DIR}/self.zip.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Temporary archives were not cleaned up")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
