file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${SOURCE_FILE}" DESTINATION "${TEST_DIR}")

set(long_dir "${TEST_DIR}")
foreach(index RANGE 1 20)
    string(APPEND long_dir "/segment-${index}-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnop")
endforeach()
file(MAKE_DIRECTORY "${long_dir}")
file(COPY "${SOURCE_FILE}" DESTINATION "${long_dir}")

set(archive_path "${long_dir}/output/archive.zip")
string(LENGTH "${archive_path}" archive_path_length)
if(archive_path_length LESS_EQUAL 1024)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Long output test path is only ${archive_path_length} bytes")
endif()

execute_process(
    COMMAND "${MINIZIP}" -0 "${archive_path}" "${long_dir}/*"
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE result
    TIMEOUT 10)

if(NOT result EQUAL 0 OR NOT EXISTS "${archive_path}")
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Long output path creation failed: ${result}")
endif()

execute_process(
    COMMAND "${MINIZIP}" -x -d "${TEST_DIR}/extracted" "${archive_path}"
    RESULT_VARIABLE extract_result
    TIMEOUT 10)

if(NOT extract_result EQUAL 0 OR NOT EXISTS "${TEST_DIR}/extracted/random.bin")
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Long input path archive could not be verified")
endif()

file(GLOB temp_archives "${archive_path}.mz_tmp.*")
if(temp_archives)
    file(REMOVE_RECURSE "${TEST_DIR}")
    message(FATAL_ERROR "Long output temporary archive was not cleaned up")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
