# gdbus_generate: run gdbus-codegen at build time (see ADR 0005).
#
#   gdbus_generate(
#       BASENAME     <name>     # output files <name>.c / <name>.h
#       XML          <path>     # Introspect XML (source of truth)
#       C_NAMESPACE  <name>     # The namespace to use for generated C code
#       IFC_PREFIX   <prefix>   # String to strip from D-Bus interface names for code and docs
#       OUT_DIR      <dir>      # directory to emit into (kept in-tree, gitignored)
#       OUT_VAR      <var>      # set to the generated .c path for add_library()
#       EXTRA_FLAGS  <args...>  # passthrough gdbus-codegen flags
#   )
function(gdbus_generate)
    cmake_parse_arguments(ARG "" "BASENAME;XML;C_NAMESPACE;IFC_PREFIX;OUT_DIR;OUT_VAR" "EXTRA_FLAGS" ${ARGN})

    find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)

    set(_c ${ARG_OUT_DIR}/${ARG_BASENAME}.c)
    set(_h ${ARG_OUT_DIR}/${ARG_BASENAME}.h)

    # Ensure OUT_DIR exists before the build runs: it is gitignored, so a fresh
    # clone has no such directory, and WORKING_DIRECTORY below cannot cd into a
    # missing one. Create it at configure time.
    file(MAKE_DIRECTORY ${ARG_OUT_DIR})

    # Run from OUT_DIR with a BARE basename: gdbus-codegen copies the
    # --generate-c-code argument verbatim into the #include and header guard, so
    # an absolute path there would leak into the generated source.
    add_custom_command(
        OUTPUT ${_c} ${_h}
        COMMAND ${GDBUS_CODEGEN} --c-namespace ${ARG_C_NAMESPACE} --interface-prefix ${ARG_IFC_PREFIX}
                ${ARG_EXTRA_FLAGS} --generate-c-code ${ARG_BASENAME} ${ARG_XML}
        WORKING_DIRECTORY ${ARG_OUT_DIR}
        DEPENDS ${ARG_XML}
        COMMENT "gdbus-codegen: ${ARG_BASENAME}"
        VERBATIM
    )

    set(${ARG_OUT_VAR} ${_c} PARENT_SCOPE)
endfunction()
