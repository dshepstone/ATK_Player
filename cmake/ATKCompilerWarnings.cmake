# ---------------------------------------------------------------------------
# ATKCompilerWarnings
#
# Provides atk_set_compiler_warnings(<target>) so every ATK target opts into a
# consistent warning set without polluting the global compile flags.
# ---------------------------------------------------------------------------

function(atk_set_compiler_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-        # stricter standard conformance
            /Zc:__cplusplus     # report the real __cplusplus value
            /Zc:preprocessor    # conforming preprocessor
            /utf-8              # source and execution charset
            /MP                 # parallel compilation
        )
        # Qt headers and the Windows SDK trigger these; they add no signal here.
        target_compile_options(${target} PRIVATE
            /wd4127  # conditional expression is constant
        )
        target_compile_definitions(${target} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
        )
    endif()
endfunction()
