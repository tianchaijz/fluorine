add_library(fluorine
    Parser.cpp
    Forwarder.cpp
    Option.cpp
    Json.cpp
    util/Fast.cpp
    util/Redis.cpp
    util/IPResolver.cpp
    )

target_link_libraries(fluorine fmt snet hiredis gzstream ${BOOSTSYS_LIBRARY} ${BOOSTPO_LIBRARY} z)
