file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")
file(RENAME "${TEST_DIR}/random.bin" "${TEST_DIR}/first.bin")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")
file(RENAME "${TEST_DIR}/random.bin" "${TEST_DIR}/second.bin")

execute_process(
    COMMAND "${MINIZIP}" -0 -o -z -p test123 archive.zip first.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE create_result
    TIMEOUT 10)

if(NOT create_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Encrypted zip-CD creation failed: ${create_result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -0 -a -z -p test123 archive.zip second.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE append_result
    TIMEOUT 10)

if(NOT append_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Encrypted zip-CD append failed: ${append_result}")
endif()

file(GLOB temp_archives "${TEST_DIR}/archive.zip.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Temporary archives were not cleaned up")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
