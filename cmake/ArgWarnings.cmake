# Warning set required by the SDD (NFR-08).
function(arg_target_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
            -Wundef -Wcast-qual -Wpointer-arith)
        if(ARG_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(ARG_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()
