# Re-glob on every build so a new document is picked up (not allowed in IDF's script pass).
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(RESCAN CONFIGURE_DEPENDS)
endif()
file(GLOB DOCS ${RESCAN} ${CMAKE_CURRENT_LIST_DIR}/../../../../documents/*.rc)
# fit/: documents designed at a fixed size, shown whole: laid out at their own size and
# scaled to the screen (rcstage::load fit).
file(GLOB DOCS_FIT ${RESCAN} ${CMAKE_CURRENT_LIST_DIR}/../../../../documents/fit/*.rc)
list(APPEND DOCS ${DOCS_FIT})
# 3D documents only where the renderer draws 3D (CONFIG_KOBENTOVG_3D; off on the Core2).
if(CONFIG_KOBENTOVG_3D)
    file(GLOB DOCS3D ${RESCAN} ${CMAKE_CURRENT_LIST_DIR}/../../../../documents/3d/*.rc)
    list(APPEND DOCS ${DOCS3D})
endif()

# A board may name the documents it wants (CONFIG_RCPLAYER_DOCUMENTS, space-separated), in that
# order; the first is shown at start. Empty: every document above.
if(CONFIG_RCPLAYER_DOCUMENTS)
    separate_arguments(_wanted UNIX_COMMAND "${CONFIG_RCPLAYER_DOCUMENTS}")
    set(_picked "")
    foreach(w ${_wanted})
        set(_found "")
        foreach(doc ${DOCS})
            get_filename_component(n ${doc} NAME_WE)
            if(n STREQUAL w)
                set(_found ${doc})
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR "CONFIG_RCPLAYER_DOCUMENTS: no document '${w}'")
        endif()
        list(APPEND _picked ${_found})
    endforeach()
    set(DOCS ${_picked})
endif()

# Generate a table of the embedded documents so main.cpp can iterate them.
set(DOC_TABLE ${CMAKE_CURRENT_BINARY_DIR}/embedded_docs.h)
set(_decl "")
set(_rows "")
foreach(doc ${DOCS})
    get_filename_component(n ${doc} NAME_WE)
    string(MAKE_C_IDENTIFIER ${n}_rc sym)
    string(APPEND _decl "extern const uint8_t _binary_${sym}_start[];\nextern const uint8_t _binary_${sym}_end[];\n")
    set(fit false)
    if(doc IN_LIST DOCS_FIT)
        set(fit true)
    endif()
    string(APPEND _rows "    {\"${n}\", _binary_${sym}_start, _binary_${sym}_end, ${fit}},\n")
endforeach()
file(WRITE ${DOC_TABLE} "#pragma once\n#include <cstdint>\n${_decl}struct EmbeddedDoc { const char* name; const uint8_t* start; const uint8_t* end; bool fit; };\nstatic const EmbeddedDoc kEmbeddedDocs[] = {\n${_rows}};\n")

idf_component_register(SRCS main.cpp console.cpp
                       INCLUDE_DIRS . ${CMAKE_CURRENT_BINARY_DIR}
                       REQUIRES rcstage board esp_timer
                       EMBED_FILES ${DOCS})
