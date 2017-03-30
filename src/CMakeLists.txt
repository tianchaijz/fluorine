add_library(fluorine
    Parser.cpp
    Forwarder.cpp
    Option.cpp
    Json.cpp
    util/Redis.cpp
    util/IPResolver.cpp
    )

target_link_libraries(fluorine snet hiredis ${BOOSTTH_LIBRARY} ${BOOSTSYS_LIBRARY} ${BOOSTPO_LIBRARY})
