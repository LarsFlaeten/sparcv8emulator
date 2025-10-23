#pragma once

class not_implemented_exception : public std::exception
{
public:
virtual char const * what() const noexcept { return "Sparc function not implemented"; }
};

class not_implemented_leon_exception : public std::exception
{
    std::string w;
public:
    not_implemented_leon_exception(std::string msg) : w(msg) {}

    virtual char const * what() const noexcept { return w.c_str(); }
};

#define LOG(fmt, ...) std::fprintf(stderr, "[%llu] " fmt "\n", \
  (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__)
  
/* Declare integer datatypes for each number of bits */
typedef std::uint_least8_t  u8;  typedef std::int_least8_t  s8;
typedef std::uint_least16_t u16; typedef std::int_least16_t s16;
typedef std::uint_least32_t u32; typedef std::int_least32_t s32;
typedef std::uint_least64_t u64; typedef std::int_least64_t s64;