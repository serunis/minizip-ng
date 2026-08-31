file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${SOURCE_FILE}" "${TEST_DIR}/first.bin" COPYONLY)
configure_file("${SECOND_FILE}" "${TEST_DIR}/second.bin" COPYONLY)

execute_process(
    COMMAND "${MINIZIP}" -9 -o archive.zip first.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE create_result
    TIMEOUT 10)

if(NOT create_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Compressed archive creation failed: ${create_result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -0 -a archive.zip second.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE append_result
    TIMEOUT 10)

if(NOT append_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Mixed-method append failed: ${append_result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -x -o -d extracted archive.zip
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE extract_result
    TIMEOUT 10)

if(NOT extract_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Mixed-method archive extraction failed: ${extract_result}")
endif()

file(SHA256 "${TEST_DIR}/first.bin" first_hash)
file(SHA256 "${TEST_DIR}/extracted/first.bin" extracted_first_hash)
file(SHA256 "${TEST_DIR}/second.bin" second_hash)
file(SHA256 "${TEST_DIR}/extracted/second.bin" extracted_second_hash)
if(NOT first_hash STREQUAL extracted_first_hash OR NOT second_hash STREQUAL extracted_second_hash)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Mixed-method append changed archived data")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
