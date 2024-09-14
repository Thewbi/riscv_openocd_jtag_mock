#ifndef IHEX_LOADER_H
#define IHEX_LOADER_H

#include <string>
#include <vector>
#include <map>
#include <stdint.h>
#include <filesystem>

// for debugging only
#include <iostream>
#include <iomanip>

// ihex line type - https://en.wikipedia.org/wiki/Intel_HEX
enum class IHexType : uint8_t {
    
    DATA = 0x00,
    END_OF_FILE = 0x01,
    EXTENDED_SEGMENT_ADDRESS = 0x02,
    START_SEGMENT_ADDRESS = 0x03,
    EXTENDED_LINEAR_ADDRESS = 0x04,
    START_LINEAR_ADDRESS = 0x05,
    BLINKY = 0x06, // ('blinky' messages / transmission protocol container) by Wayne and Layne
    BLOCK_START = 0x0A,
    BLOCK_END = 0x0B,
    PADDED_DATA = 0x0C,
    CUSTOM_DATA = 0x0D,
    OTHER_DATA = 0x0E, // by the BBC/Micro:bit Educational Foundation
    CODE_SEGMENT = 0x81,
    DATA_SEGMENT = 0x82,
    STACK_SEGMENT = 0x83,
    EXTRA_SEGMENT = 0x84,
    PARAGRAPH_ADDRESS_FOR_ABSOLUTE_CODE = 0x85,
    PARAGRAPH_ADDRESS_FOR_ABSOLUTE_DATA = 0x86,
    PARAGRAPH_ADDRESS_FOR_ABSOLUTE_STACK = 0x87,
    PARAGRAPH_ADDRESS_FOR_ABSOLUTE_EXTRA = 0x88,

    UNKNOWN = 0xFF
};

class IHexLoader {

    public:
        uint8_t process_hex_line(const std::string& line);
        void print_ihex_type(const IHexType& ihex_type);

    public:
        //std::vector<std::vector<uint32_t>> segments;
        uint32_t current_address = 0x00;

        std::map<uint32_t, uint32_t*> segments;

};

#endif