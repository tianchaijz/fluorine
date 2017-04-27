add_executable(Split
    Split.cpp
    )
target_link_libraries(Split fmt ${BOOSTPO_LIBRARY} ${GZSTREAM_LIBRARY} z)
