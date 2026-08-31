file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${SOURCE_FILE}" "${TEST_DIR}/archive.zip" COPYONLY)
configure_file("${SECOND_FILE}" "${TEST_DIR}/second.bin" COPYONLY)
file(SHA256 "${TEST_DIR}/archive.zip" archive_hash_before)

execute_process(
    COMMAND "${MINIZIP}" -0 -a archive.zip second.bin
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE append_result
    TIMEOUT 10)

if(append_result EQUAL 0)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Appending to an invalid archive unexpectedly succeeded")
endif()

file(SHA256 "${TEST_DIR}/archive.zip" archive_hash_after)
if(NOT archive_hash_after STREQUAL archive_hash_before)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Failed append changed the original archive")
endif()

file(GLOB temp_archives "${TEST_DIR}/archive.zip.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Failed append left temporary archives: ${temp_archives}")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
