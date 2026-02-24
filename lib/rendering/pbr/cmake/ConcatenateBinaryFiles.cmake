# Copyright 2026 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Cross-platform binary file concatenation for PMJ02 sampling chunks.
#
# Usage: cmake -DINPUT_FILES="file1;file2;file3" -DOUTPUT_FILE="output" -P ConcatenateBinaryFiles.cmake

if(NOT DEFINED INPUT_FILES OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILES and OUTPUT_FILE must be specified")
endif()

# Convert space-separated INPUT_FILES string to a list if needed
if(NOT INPUT_FILES MATCHES ";")
    string(REPLACE " " ";" INPUT_FILES "${INPUT_FILES}")
endif()

# Use platform-specific tools to concatenate binary files
file(WRITE "${OUTPUT_FILE}" "")

foreach(input_file ${INPUT_FILES})
    file(READ "${input_file}" content HEX)
    string(APPEND output_hex "${content}")
endforeach()

if(CMAKE_HOST_WIN32)
    # On Windows, use type command
    string(REPLACE ";" " " files_list "${INPUT_FILES}")
    execute_process(
        COMMAND cmd /c "type ${files_list} > ${OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE result
    )
else()
    # On Unix-like systems, use cat command
    execute_process(
        COMMAND cat ${INPUT_FILES}
        OUTPUT_FILE "${OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE result
    )
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to concatenate binary files")
endif()
