cmake_minimum_required(VERSION 3.3)
project(ArduinoLibs)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    Debug/examples/DebugExample/DebugExample.ino
    Debug/Debug.cpp
    Debug/Debug.h
    Debug/library.json
    Debug/README.md
    EEPromUtils/EEPromUtils.cpp
    EEPromUtils/EEPromUtils.h
    EEPromUtils/library.json
    EEPromUtils/README.md
    GeneralUtils/GeneralUtils.cpp
    GeneralUtils/GeneralUtils.h
    LCD/LCD.cpp
    LCD/LCD.h
    Menu/Menu.cpp
    Menu/Menu.h
    MPR121/examples/MPR121BasicUse/MPR121BasicUse.ino
    MPR121/examples/MPR121Multiple/MPR121Multiple.ino
    MPR121/library.json
    MPR121/MPR121.cpp
    MPR121/MPR121.h
    MPR121/README.md
    Pins/Actions.cpp
    Pins/Pins.cpp
    Pins/Pins.h
    PixelUtil/PixelUtil.cpp
    PixelUtil/PixelUtil.h
        RFM69Socket/examples/RFM69_SocketTool/RFM69_SocketTool.ino
    RFM69Socket/examples/RawNode/RawNode.ino
    RFM69Socket/examples/RawGateway/RawGateway.ino
    RFM69Socket/RFM69Socket.h
    RFM69Socket/RFM69Socket.cpp
    RFM69Socket/examples/RFM69_Tool/RFM69_Tool.ino
    RS485Utils/examples/RawSerial/SerialMaster/SerialMaster.ino
    RS485Utils/examples/RawSerial/SerialSlave/SerialSlave.ino
    RS485Utils/examples/RawSerial/SerialTest/SerialTest.ino
    RS485Utils/examples/RawSerial/README.txt
    RS485Utils/examples/SocketTest/SocketTestMaster/SocketTestMaster.ino
    RS485Utils/examples/SocketTest/SocketTestSlave/SocketTestSlave.ino
    RS485Utils/README.txt
    RS485Utils/RS485Utils.cpp
    RS485Utils/RS485Utils.h
    SerialCLI/examples/ExampleCLI/ExampleCLI.ino
    SerialCLI/library.json
    SerialCLI/README.md
    SerialCLI/SerialCLI.cpp
    SerialCLI/SerialCLI.h
    Shift/Shift.cpp
    Shift/Shift.h
    ShiftBar/ShiftBar.cpp
    ShiftBar/ShiftBar.h
    Socket/Socket.h
    XBeeSocket/examples/XBeeSocketTest/XBeeSocketTest.ino
    XBeeSocket/XBeeSocket.cpp
    XBeeSocket/XBeeSocket.h
    README.md
    PixelUtil/examples/PixelExample/PixelExample.ino
        PixelUtil/PixelUtil_config.h)

add_executable(ArduinoLibs ${SOURCE_FILES})

# Core Arduino Libraries
set(ARDUINO_HOME /Applications/Arduino_1.6.13.app/Contents/Java)
include_directories(${ARDUINO_HOME}/hardware/arduino/avr/cores/arduino/)
include_directories(${ARDUINO_HOME}/hardware/arduino/avr/libraries/EEPROM)
include_directories(${ARDUINO_HOME}/hardware/arduino/avr/libraries/SoftwareSerial)
include_directories(${ARDUINO_HOME}/hardware/arduino/avr/libraries/SPI)
include_directories(${ARDUINO_HOME}/hardware/arduino/avr/libraries/Wire)

# Arduino libraries
set(LIBRARIES /Users/amp/Dropbox/Arduino/libraries)
include_directories(${LIBRARIES}/FastLED)
include_directories(${LIBRARIES}/RFM69)
include_directories(${LIBRARIES}/RS485_non_blocking)
include_directories(${LIBRARIES}/Xbee)

# AMP's ArduinoLibs libraries
set(ARDLIBS_HOME /Users/amp/Dropbox/Arduino/ArduinoLibs)
include_directories(${ARDLIBS_HOME}/Debug)
include_directories(${ARDLIBS_HOME}/MPR121)
include_directories(${ARDLIBS_HOME}/PixelUtil)
include_directories(${ARDLIBS_HOME}/RFM69Socket)
include_directories(${ARDLIBS_HOME}/RS485Utils)
include_directories(${ARDLIBS_HOME}/SerialCLI)
include_directories(${ARDLIBS_HOME}/Socket)

