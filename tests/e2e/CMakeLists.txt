# End-to-end tests: run the programs from examples/ and check exit code,
# stdout and stderr. Run only these with `ctest -L e2e`.
#
#   arg_e2e(<name> <target> CODE <n> [ARGS a b ...] [OUT regex]
#           [ERR regex] [ENV NAME=VALUE ...])
#
# Help output is checked loosely (exit 0 and "Usage:") on purpose: the
# exact layout belongs to the help renderer and its own tests.

set(ARG_E2E_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)

function(arg_e2e name target)
    cmake_parse_arguments(E "" "CODE;OUT;ERR" "ARGS;ENV" ${ARGN})
    string(REPLACE ";" "|" args "${E_ARGS}")
    string(REPLACE ";" "|" env "${E_ENV}")
    add_test(NAME e2e.${name}
        COMMAND ${CMAKE_COMMAND}
            "-DEXE=$<TARGET_FILE:${target}>"
            "-DARGS=${args}"
            "-DEXPECT_CODE=${E_CODE}"
            "-DEXPECT_OUT=${E_OUT}"
            "-DEXPECT_ERR=${E_ERR}"
            "-DENV=${env}"
            -P ${ARG_E2E_SCRIPT})
    set_tests_properties(e2e.${name} PROPERTIES LABELS e2e TIMEOUT 30)
endfunction()

# rules.c is a stub until issue #7 lands; its cases wait for it.
file(READ ${PROJECT_SOURCE_DIR}/src/rules.c ARG_RULES_SRC)
string(FIND "${ARG_RULES_SRC}" "stub, replaced by issue" ARG_RULES_STUB)

# ---- 00-minimal: shrink --------------------------------------------

arg_e2e(shrink.basic shrink CODE 0 ARGS a.png
    OUT "^a.png at quality 80\n$" ERR "^$")
arg_e2e(shrink.flags shrink CODE 0 ARGS -v -q 50 a.png b.png
    OUT "shrinking a.png at quality 50\nshrinking b.png at quality 50")
arg_e2e(shrink.bundle shrink CODE 0 ARGS -vq7 x
    OUT "^shrinking x at quality 7\n$")
arg_e2e(shrink.long_eq shrink CODE 0 ARGS --quality=1 x
    OUT "x at quality 1")
arg_e2e(shrink.interleaved shrink CODE 0 ARGS a -q 90 b
    OUT "a at quality 90\nb at quality 90")
arg_e2e(shrink.range_high shrink CODE 2 ARGS -q 101 a.png
    OUT "^$" ERR "out of range: must be between 1 and 100")
arg_e2e(shrink.range_low shrink CODE 2 ARGS --quality 0 a.png
    ERR "value '0' for '--quality")
arg_e2e(shrink.bad_int shrink CODE 2 ARGS -q many a.png
    ERR "invalid value 'many'")
arg_e2e(shrink.did_you_mean shrink CODE 2 ARGS --qualty 5 a
    ERR "unknown option '--qualty'.*Did you mean '--quality'")
arg_e2e(shrink.missing_arg shrink CODE 2
    ERR "missing required argument <image>")
arg_e2e(shrink.missing_value shrink CODE 2 ARGS a -q
    ERR "requires a value")
arg_e2e(shrink.dashdash shrink CODE 0 ARGS -- -x.png --help
    OUT "-x.png at quality 80\n--help at quality 80")
arg_e2e(shrink.help shrink CODE 0 ARGS --help OUT "Usage:")
arg_e2e(shrink.help_short shrink CODE 0 ARGS a -h OUT "Usage:")
arg_e2e(shrink.no_version shrink CODE 2 ARGS --version
    ERR "unknown option '--version'")

# ---- 01-pkg: subcommands, aliases, globals, env, ARG_USE ------------

arg_e2e(pkg.install pkg CODE 0 ARGS install -f vim git
    OUT "install vim into / \\(forced\\)\ninstall git into / \\(forced\\)")
arg_e2e(pkg.alias pkg CODE 0 ARGS i vim OUT "^install vim into /\n$")
arg_e2e(pkg.alias_rm pkg CODE 0 ARGS rm a b OUT "^remove a\nremove b\n$")
arg_e2e(pkg.root_opt pkg CODE 0 ARGS --root /x install vim
    OUT "install vim into /x")
arg_e2e(pkg.env pkg CODE 0 ARGS install vim ENV PKG_ROOT=/opt
    OUT "install vim into /opt")
arg_e2e(pkg.cli_beats_env pkg CODE 0 ARGS --root=/cli install vim
    ENV PKG_ROOT=/opt OUT "install vim into /cli")
arg_e2e(pkg.root_is_local pkg CODE 2 ARGS install --root /x vim
    ERR "unknown option '--root'")
arg_e2e(pkg.global_before pkg CODE 0 ARGS -v repo add main http://e
    OUT "adding repo ...\nmain -> http://e")
arg_e2e(pkg.global_after pkg CODE 0 ARGS repo add main http://e --verbose
    OUT "adding repo ...\nmain -> http://e")
arg_e2e(pkg.no_global pkg CODE 0 ARGS repo add main http://e
    OUT "^main -> http://e\n$")
arg_e2e(pkg.use_alias pkg CODE 0 ARGS repo ls OUT "^main https://")
arg_e2e(pkg.unknown_cmd pkg CODE 2 ARGS instal x
    ERR "unknown command 'instal'.*Did you mean 'install'")
arg_e2e(pkg.missing_cmd pkg CODE 2 ERR "missing command")
arg_e2e(pkg.group_cmd pkg CODE 2 ARGS repo ERR "pkg repo")
arg_e2e(pkg.missing_url pkg CODE 2 ARGS repo add main
    ERR "pkg repo add: missing required argument <url>")
arg_e2e(pkg.too_many pkg CODE 2 ARGS repo add a b c
    ERR "unexpected argument 'c'")
arg_e2e(pkg.do pkg CODE 0 ARGS --list-formats OUT "^tar zip zst\n$")
arg_e2e(pkg.help pkg CODE 0 ARGS --help OUT "Usage:")
arg_e2e(pkg.sub_help pkg CODE 0 ARGS repo add --help OUT "Usage:")

# ---- 02-level1: static spec, .to bindings, path dispatch ------------

arg_e2e(todo.add todo CODE 0 ARGS --file w.txt add "write tests"
    OUT "^added \"write tests\" to w.txt\n$")
arg_e2e(todo.bind_default todo CODE 0 ARGS list
    OUT "showing up to 10 items of todo.txt")
arg_e2e(todo.bind_value todo CODE 0 ARGS list -n 3
    OUT "showing up to 3 items")
arg_e2e(todo.range todo CODE 2 ARGS list -n 0 ERR "must be at least 1")
arg_e2e(todo.handler todo CODE 0 ARGS stats
    OUT "you has 0 open items in todo.txt")
arg_e2e(todo.version todo CODE 0 ARGS --version OUT "^todo 1.0.0\n$")
arg_e2e(todo.version_short todo CODE 0 ARGS -V OUT "^todo 1.0.0\n$")
arg_e2e(todo.missing_cmd todo CODE 2 ERR "missing command")

# ---- 03-tokenizer -------------------------------------------------

arg_e2e(tokens.mix tokens CODE 0
    ARGS -vofile --name=x --flag pos -- --not -o
    OUT "^short v\nshort o = file\nlong name = x\nlong flag\npositional pos\nend\npositional --not\npositional -o\n$")
arg_e2e(tokens.separate_value tokens CODE 0 ARGS -o -x
    OUT "^short o = -x\n$")
arg_e2e(tokens.dash tokens CODE 0 ARGS - OUT "^positional -\n$")
arg_e2e(tokens.no_value tokens CODE 2 ARGS -o ERR "-o needs a value")

# ---- 04-custom-type -----------------------------------------------

arg_e2e(paint.default paint CODE 0 OUT "^accent #ff8800, png, plain\n$")
arg_e2e(paint.values paint CODE 0
    ARGS --color "#00ff7f" --format jpg --fill
    OUT "accent #00ff7f, jpg, filled")
arg_e2e(paint.bad_color paint CODE 2 ARGS -c red
    ERR "invalid value 'red' for '--color.*expected #RRGGBB")
arg_e2e(paint.bad_hex paint CODE 2 ARGS -c "#12345g"
    ERR "invalid hex digit")
arg_e2e(paint.bad_enum paint CODE 2 ARGS -f gif ERR "invalid value 'gif'")
arg_e2e(paint.mutex paint CODE 2 ARGS --fill --outline
    OUT "^$" ERR "fill")
if(ARG_RULES_STUB GREATER -1)
    set_tests_properties(e2e.paint.mutex PROPERTIES DISABLED TRUE)
endif()

# ---- 05-actions: ARG_ON, ARG_DO, ARG_RUN_EX ---------------------------

arg_e2e(search.on_order search CODE 0 ARGS -I b -I a x
    OUT "^b/x\na/x\n$")
arg_e2e(search.on_attached search CODE 0 ARGS -Iinc --include=lib h
    OUT "^inc/h\nlib/h\n$")
arg_e2e(search.on_fail search CODE 1 ARGS -I ../secret x
    OUT "^$" ERR "'..' is not allowed in a search path")
arg_e2e(search.on_no_value search CODE 0 ARGS -q -I a x OUT "^$")
arg_e2e(search.do_fail search CODE 1 ARGS --check OUT "^$")
arg_e2e(search.do_ok search CODE 0 ARGS -I a --check x OUT "^$")

# ---- 06-no-exit -------------------------------------------------------

arg_e2e(safe.ok safe CODE 0 ARGS --port 8080
    OUT "^connecting to localhost:8080\n$")
arg_e2e(safe.range safe CODE 2 ARGS -p 0
    ERR "^safe: ARG_E_RANGE for 'port' at '0': must be between 1 and 65535\n$")
arg_e2e(safe.suggest safe CODE 2 ARGS --prot 80
    ERR "ARG_E_UNKNOWN_OPTION at '--prot' \\(did you mean '--port'\\?\\)")
arg_e2e(safe.required safe CODE 2 ERR "^safe: ARG_E_MISSING_REQUIRED for 'port'")
arg_e2e(safe.help safe CODE 0 ARGS --help OUT "Usage:")
