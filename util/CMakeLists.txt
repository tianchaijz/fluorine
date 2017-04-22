add_executable(Split
    Split.cpp
    )
target_link_libraries(Split ${BOOSTSYS_LIBRARY} ${BOOSTPO_LIBRARY} ${BOOSTIOS_LIBRARY} ${GZSTREAM_LIBRARY} z)
