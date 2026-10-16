qt_add_executable(CaroClient WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    BoardWidget.h
    BoardWidget.cpp
)
target_link_libraries(CaroClient PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)
