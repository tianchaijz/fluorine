add_library(fluorine
    Forwarder.cpp
    Option.cpp
    util/IPResolver.cpp
    )

target_link_libraries(fluorine snet ${BOOSTTH_LIBRARY} ${BOOSTSYS_LIBRARY} ${BOOSTPO_LIBRARY})
