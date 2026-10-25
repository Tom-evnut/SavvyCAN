cmake_minimum_required(VERSION 3.21)
project(SavvyCAN_unit_tests LANGUAGES CXX)

set(QT_MIN_VERSION 6.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find Qt6
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core Test SerialBus Widgets)

# Directories where SavvyCAN headers live
set(SAVVYCAN_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../bus_protocols
    ${CMAKE_CURRENT_SOURCE_DIR}/../connections
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

###### iso-tp handler test ######
set(TEST_SOURCES_ISOTP_HANDLER
    test_isotp_handler.cpp
    ../bus_protocols/isotp_handler.cpp  # the class under test
    ../canfilter.cpp
)

add_executable(TestIsoTpHandler ${TEST_SOURCES_ISOTP_HANDLER})

target_include_directories(TestIsoTpHandler PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestIsoTpHandler
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### uds handler test ######
set(TEST_SOURCES_UDS_HANDLER
    test_uds_handler.cpp
    ../bus_protocols/uds_handler.cpp  # the class under test
    ../bus_protocols/isotp_handler.cpp

    ../canfilter.cpp
    ../utility.cpp
)

add_executable(TestUdsHandler ${TEST_SOURCES_UDS_HANDLER})

target_include_directories(TestUdsHandler PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestUdsHandler
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
    Qt6::Widgets
)

###### LIN frame test ######
add_executable(TestLinFrame test_lin_frame.cpp)  # CommFrame is header-only

target_include_directories(TestLinFrame PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestLinFrame
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### APGDT001 parser test ######
add_executable(TestApgdt001Parser
    test_apgdt001_parser.cpp
    ../connections/apgdt001parser.cpp  # the class under test
)

target_include_directories(TestApgdt001Parser PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestApgdt001Parser
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### APGDT001 session test ######
add_executable(TestApgdt001Session
    test_apgdt001_session.cpp
    ../connections/apgdt001session.cpp  # the class under test
    ../connections/apgdt001parser.cpp
)

target_include_directories(TestApgdt001Session PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestApgdt001Session
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### LIN display and native CSV test ######
add_executable(TestLinFormat
    test_lin_format.cpp
    ../framefileio.cpp  # native CSV, with what it needs
    ../blfhandler.cpp
    ../pcaplite.cpp
    ../utility.cpp
)

target_include_directories(TestLinFormat PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestLinFormat
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
    Qt6::Widgets
)

###### LDF test ######
add_executable(TestLdf
    test_ldf.cpp
    ../dbc/ldfparser.cpp  # the classes under test
    ../dbc/ldftodbc.cpp
    ../dbc/linschedule.cpp
    ../dbc/dbchandler.cpp
    ../dbc/dbc_classes.cpp
    ../utility.cpp
    ../framefileio.cpp  # LIN text logs, decoded with real LDFs
    ../blfhandler.cpp
    ../pcaplite.cpp
)

target_include_directories(TestLdf PRIVATE ${SAVVYCAN_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../dbc)
# real LDFs in the git-ignored test/lin-data/ldf are tested too, when there are any
target_compile_definitions(TestLdf PRIVATE SAVVYCAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(TestLdf
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
    Qt6::Widgets
)

###### LIN schedule tables and runner test ######
add_executable(TestLinSchedule
    test_lin_schedule.cpp
    ../dbc/linschedule.cpp  # the classes under test
    ../linschedulerunner.cpp
    ../dbc/ldfparser.cpp
)

target_include_directories(TestLinSchedule PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestLinSchedule
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### PEAK LIN test ######
add_executable(TestPlin
    test_plin.cpp
    ../connections/plinmonitor.cpp  # the class under test
    ../connections/plindll.cpp      # without the PEAK driver: checks it fails cleanly, and its struct layouts
    ../connections/plinchardev.cpp  # PEAK's Linux driver: conversions and finding channels in a made-up sysfs
)

target_include_directories(TestPlin PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestPlin
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### CANBus test ######
add_executable(TestCanBus
    test_canbus.cpp
    ../connections/canbus.cpp  # the class under test
)

target_include_directories(TestCanBus PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestCanBus
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)


###### BLF test ######
add_executable(TestBlf
    test_blf.cpp
    ../blfhandler.cpp  # the class under test
)

target_include_directories(TestBlf PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestBlf
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)


# Enable ctest integration
enable_testing()
###### LIN schedule analysis test ######
add_executable(TestLinScheduleAnalysis
    test_lin_schedule_analysis.cpp
    ../linscheduleanalysis.cpp  # the code under test
)

target_include_directories(TestLinScheduleAnalysis PRIVATE ${SAVVYCAN_INCLUDE_DIRS})
# real LIN text logs in the git-ignored test/lin-data/captures are analysed too, when there are any
target_compile_definitions(TestLinScheduleAnalysis PRIVATE SAVVYCAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(TestLinScheduleAnalysis
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

###### LIN diagnostic transport layer test ######
add_executable(TestLinTp
    test_lintp.cpp
    ../bus_protocols/lintp.cpp  # the code under test
)

target_include_directories(TestLinTp PRIVATE ${SAVVYCAN_INCLUDE_DIRS})

target_link_libraries(TestLinTp
    Qt6::Core
    Qt6::Test
    Qt6::SerialBus
)

add_test(NAME TestIsoTpHandler COMMAND TestIsoTpHandler)
add_test(NAME TestUdsHandler COMMAND TestUdsHandler)
add_test(NAME TestLinFrame COMMAND TestLinFrame)
add_test(NAME TestApgdt001Parser COMMAND TestApgdt001Parser)
add_test(NAME TestCanBus COMMAND TestCanBus)
add_test(NAME TestApgdt001Session COMMAND TestApgdt001Session)
add_test(NAME TestLinFormat COMMAND TestLinFormat)
add_test(NAME TestLdf COMMAND TestLdf)
add_test(NAME TestLinTp COMMAND TestLinTp)
add_test(NAME TestLinSchedule COMMAND TestLinSchedule)
add_test(NAME TestPlin COMMAND TestPlin)
add_test(NAME TestBlf COMMAND TestBlf)
add_test(NAME TestLinScheduleAnalysis COMMAND TestLinScheduleAnalysis)
# the DBC classes never free a file's messages and signals (GUI windows keep pointers to them), so under
# AddressSanitizer those leaks are suppressed: see lsan_dbc_ownership.supp. Other leaks still fail the test.
# The slow unwinder gets allocation stacks through Qt's libraries, so the suppressions can name our functions.
set_tests_properties(TestLdf PROPERTIES ENVIRONMENT
    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_dbc_ownership.supp;ASAN_OPTIONS=fast_unwind_on_malloc=0")
