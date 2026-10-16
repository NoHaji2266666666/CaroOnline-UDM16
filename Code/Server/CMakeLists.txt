qt_add_executable(CaroServer
    main.cpp
    CaroServer.h
    CaroServer.cpp
)
target_link_libraries(CaroServer PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
target_compile_definitions(CaroServer PRIVATE QT_NO_CAST_FROM_ASCII)
