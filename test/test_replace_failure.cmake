file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/archive.zip")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")

execute_process(
    COMMAND "${MINIZIP}" -0 -o archive.zip random.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE result
    TIMEOUT 10)

if(result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Replacing a directory unexpectedly succeeded")
endif()

file(GLOB temp_archives "${TEST_DIR}/archive.zip.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Temporary archive remained after replacement failure: ${temp_archives}")
endif()

if(NOT IS_DIRECTORY "${TEST_DIR}/archive.zip")
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Failed replacement changed the destination directory")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
