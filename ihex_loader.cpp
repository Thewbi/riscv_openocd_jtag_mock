#include "ihex_loader.h"

// https://stackoverflow.com/questions/17261798/converting-a-hex-string-to-a-byte-array
std::vector<uint8_t> hext_to_bytes(const std::string& hex) {
  std::vector<uint8_t> bytes;

  for (unsigned int i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    uint8_t byte = (uint8_t) strtol(byteString.c_str(), NULL, 16);
    bytes.push_back(byte);
  }

  return bytes;
}

/// @brief 
/// from: http://www.dlwrr.com/electronics/tools/hexview/hexview.html
/// The basic formatting is this:
/// Each line must have the format ":LLAAAATT<data>SS"
/// Where:
///  1st char is a colon
///  Everything afterward are 2-hex-char (high-nibble/low-nibble) representations of bytes, eg. "12"==0x12
///  Those bytes are:
///     LL is the length
///     AAAA is the address
///     TT is the record type
///     <data> is 'LL' of data bytes
///     SS is the checksum
///  The line sequence is:
///     An Address record (rectype=EXTSEG, STARTSEG, EXTLINADDR, or STARTLINADDR)
///     Any number of Data records (rectype=DATA)
///     Either:
///         An EOF record, or
///         Another Address record (with attendant data records)
/// @param line 
/// @return 
uint8_t IHexLoader::process_hex_line(const std::string& line) {

    // every line must start with a colon
    if (line.at(0) != ':') {
        return -1;
    }

    std::vector<uint8_t> byte_array = hext_to_bytes(line.substr(1));

    // DEBUG output the bytes
    // for (const uint8_t data : byte_array) {
    //     std::cout << std::setfill('0') << std::setw(2) << std::hex << +data << " ";
    // }
    // std::cout << "\n";

    uint8_t length = byte_array.at(0);

    uint16_t address = byte_array.at(1) << 8 | byte_array.at(2);

    uint8_t type = byte_array.at(3);
    IHexType ihex_type = static_cast<IHexType>(type);
    //print_ihex_type(ihex_type);

    uint8_t checksum = byte_array.at(byte_array.size()-2);

    //
    // DEBUG produce the same output as http://www.dlwrr.com/electronics/tools/hexview/hexview.html
    //

    std::cout << std::setfill('0') << std::setw(2) << std::hex << +length << " " << std::setw(4) << +address << " " << std::setw(2) << +type;
    std::cout << " ";
    for (size_t i = 4; i < byte_array.size()-2; i++) {
        std::cout << std::setfill('0') << std::setw(2) << std::hex << +byte_array.at(i);
    }
    std::cout << " " << +checksum << "\n";

    switch (ihex_type) {

        case IHexType::DATA:
        {
            // the first data line for an address will reserve memory
            if (segments.find(current_address) == segments.end()) {
                uint32_t* segment_ptr = new uint32_t[16384];
                segments.insert(std::pair<uint32_t, uint32_t*>(current_address, segment_ptr));
            }

            int word_read = 0;
            uint32_t word = 0;
            for (size_t i = 4; i < byte_array.size()-2; i++) {

                word <<= 8;
                word = word | byte_array.at(i);
                word_read++;
                
                if (word_read == 4) {

                    uint32_t absolute_address = current_address | address;
                    std::cout << std::setfill('0') << std::setw(8) << std::hex << +word << " -> " << absolute_address << std::endl;

                    segments.at(current_address)[address/4] = word;

                    word_read = 0;
                    word = 0;

                    address += 4;
                }
            }

            if (word_read > 0) {

                uint32_t absolute_address = current_address | address;
                std::cout << std::setfill('0') << std::setw(8) << std::hex << +word << " -> " << absolute_address << std::endl;

                segments.at(current_address)[address/4] = word;
            }
        }
        break;

        case IHexType::EXTENDED_SEGMENT_ADDRESS:
        case IHexType::EXTENDED_LINEAR_ADDRESS:
        {
            current_address = byte_array.at(4) << 24 | byte_array.at(5) << 16;
        }
        break;

        case IHexType::START_SEGMENT_ADDRESS:
        case IHexType::START_LINEAR_ADDRESS:
        {
            int word_read = 0;
            uint32_t word = 0;
            for (size_t i = 4; i < byte_array.size()-2; i++) {
                word <<= 8;
                word = word | byte_array.at(i);
                word_read++;
                
                if (word_read == 4) {
                    start_address = word;
                }
            }
        }

        default:
            break;

    }

    return 0;
}

void IHexLoader::print_ihex_type(const IHexType& ihex_type) {
    
    switch (ihex_type) {
        case IHexType::DATA:
            std::cout << "DATA" << "\n";
            break;

        case IHexType::END_OF_FILE:
            std::cout << "END_OF_FILE" << "\n";
            break;

        case IHexType::EXTENDED_SEGMENT_ADDRESS:
            std::cout << "EXTENDED_SEGMENT_ADDRESS" << "\n";
            break;

        case IHexType::START_SEGMENT_ADDRESS:
            std::cout << "START_SEGMENT_ADDRESS" << "\n";
            break;

        case IHexType::EXTENDED_LINEAR_ADDRESS:
            std::cout << "EXTENDED_LINEAR_ADDRESS" << "\n";
            break;

        case IHexType::START_LINEAR_ADDRESS:
            std::cout << "START_LINEAR_ADDRESS" << "\n";
            break;

        default:
            std::cout << "UNKNOWN" << "\n";
            break;
    }
}