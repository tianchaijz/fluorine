add_executable(t_redis
    t_redis.cpp
    )
target_link_libraries(t_redis fluorine fmt)

add_executable(t_json
    t_json.cpp
    )

add_executable(t_gzip
    t_gzip.cpp
    )
target_link_libraries(t_gzip ${BOOSTIOS_LIBRARY} z)
