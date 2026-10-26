# Sets PROJECT_VER to "<commit time>-<7-hex short hash>", with "+" appended
# when tracked files differ from HEAD, or "0-unknown" without git. Include it
# after project.cmake and before project().
#
# PlatformIO re-runs CMake only when CMakeLists.txt or sdkconfig change, so a
# local build's stamp can lag behind HEAD. Mac pushes therefore send their own
# commit time rather than reading this one.
execute_process(
    COMMAND git log -1 --format=%ct-%h --abbrev=7
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE CYD_GIT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CYD_GIT_LOG_RESULT
    ERROR_QUIET)
if(CYD_GIT_LOG_RESULT EQUAL 0 AND CYD_GIT_VER)
    execute_process(
        COMMAND git diff --quiet HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESULT_VARIABLE CYD_GIT_DIFF_RESULT
        ERROR_QUIET)
    if(CYD_GIT_DIFF_RESULT EQUAL 1)
        set(CYD_GIT_VER "${CYD_GIT_VER}+")
    endif()
    set(PROJECT_VER "${CYD_GIT_VER}")
else()
    set(PROJECT_VER "0-unknown")
endif()
