#include <iostream>
#include <string>
#include <stdexcept>

static inline int fromhex(char v)
{
    if (v >= '0' && v <= '9') {
        return v - '0';
    } else if (v >= 'A' && v <= 'F') {
        return v - 'A' + 10;
    } else if (v >= 'a' && v <= 'f') {
        return v - 'a' + 10;
    } else {
        return 0;
    }
}

static inline char tohex(int v)
{
    if (v < 10) {
        return v + '0';
    } else {
        return v - 10 + 'a';
    }
}


static inline std::string byte_to_hex(u8 byte) {
    std::string res;

    res += tohex(byte >> 4);
    res += tohex(byte & 0xf);

    return res;
}

static inline std::string u32_to_hexstr(u32 value) {
    u8 byte1 = (value >> 24) & 0xff;
    u8 byte2 = (value >> 16) & 0xff;
    u8 byte3 = (value >> 8 ) & 0xff;
    u8 byte4 = value & 0x000000ff;

    std::string res;

    res += byte_to_hex(byte1);
    res += byte_to_hex(byte2);
    res += byte_to_hex(byte3);
    res += byte_to_hex(byte4);

    return res;
}

static inline u32 hexstr_to_u32(const std::string& hexString) {
   	

	try {
        // Ensure the string starts with "0x" or "0X"
        std::size_t startIdx = 0;
        if (hexString.rfind("0x", 0) == 0 || hexString.rfind("0X", 0) == 0) {
            startIdx = 2;
        }

        // Convert the string to an integer (base 16)
        u32 result = std::stoul(hexString.substr(startIdx), nullptr, 16);
        return (u32)result;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        throw;
    } catch (const std::out_of_range& e) {
        std::cerr << "Out of range: " << e.what() << std::endl;
        throw;
    }	

}


