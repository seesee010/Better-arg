# Example programs. Each one is small enough to read in a minute and is
# also run by the end-to-end tests in tests/e2e/.

function(arg_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE arg)
    arg_target_warnings(${name})
endfunction()

arg_example(shrink 00-minimal/shrink.c)
arg_example(pkg 01-pkg/main.c 01-pkg/repo.c)
arg_example(todo 02-level1/todo.c)
arg_example(tokens 03-tokenizer/tokens.c)
arg_example(paint 04-custom-type/paint.c)
arg_example(search 05-actions/search.c)
arg_example(safe 06-no-exit/safe.c)
