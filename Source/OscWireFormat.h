#pragma once

#include <cstddef>

/*
    OscWireFormat  (B16)

    Shared, documented description of the OSC wire format that the
    "Spektra / Audience" control surface speaks, plus a tiny, allocation-free
    parser for it.

    WIRE FORMAT
    -----------
    Incoming control-surface messages use the address pattern:

        /cs/<row>/<col>/finger<n>/<param>

    where:
        /cs/        literal prefix ("control surface"). Case-insensitive.
        <row>       exactly one letter A..Z (case-insensitive) -> row index 0..25.
        <col>       one or more decimal digits -> source/participant id. Must
                    be in [0, MAX_OSC_SOURCES). The digits must be followed
                    by '/'.
        finger<n>   the exact lower-case token "finger" followed by one digit
                    0..9. The finger id remains part of the parsed address.
        <param>     the trailing segment, one of (case-insensitive):
                        "on"   -> note on/off    (arg: int/float, !=0 => on)
                        "off"  -> note off       (no arg required)
                        "u"    -> horizontal val (arg: float 0..1)
                        "v"    -> vertical value (arg: float 0..1)
                        "line" -> horizontal val (arg: float 0..127)

    This header only parses the *address* (row / source / finger / param). Argument
    decoding and clamping stay in the caller, because they depend on the
    OSC argument types (OSCMessage), not on the address string.

    DESIGN NOTES
    ------------
    - Header-only / inline so it can be shared without touching the build.
    - The parser takes a raw, NUL-terminated UTF-8 C string so callers can
      parse straight off juce::OSCAddressPattern's underlying storage without
      allocating a juce::String per message on the OSC thread (see B26).
    - The finger token is intentionally strict so malformed or unsupported
      finger ids cannot collapse onto the same participant voice.
*/
namespace osc_wire
{
    // Address layout constants.
    inline constexpr char        kPrefix[]     = "/cs/"; // control-surface prefix
    inline constexpr std::size_t kPrefixLen    = 4;      // length of kPrefix ("/cs/")
    inline constexpr char        kRowFirst     = 'A';    // first valid row letter
    inline constexpr char        kRowLast      = 'Z';    // last valid row letter
    inline constexpr int         kMaxRows      = 26;     // 'A'..'Z'

    // Trailing parameter tokens (matched case-insensitively).
    inline constexpr char kParamOn[]   = "on";
    inline constexpr char kParamOff[]  = "off";
    inline constexpr char kParamU[]    = "u";
    inline constexpr char kParamV[]    = "v";
    inline constexpr char kParamLine[] = "line";
    inline constexpr char kFingerPrefix[] = "finger";
    inline constexpr std::size_t kFingerPrefixLen = 6;

    // Parsed parameter kind. Unknown trailing tokens map to None.
    enum class Param { None, On, Off, U, V, Line };

    // ASCII lower-case (locale-independent), matching the original parser.
    inline constexpr char toLowerAscii (char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
    }

    // Case-insensitive full-string compare of a NUL-terminated C string
    // against a token. Equivalent to the original "matches" lambda.
    inline bool equalsIgnoreCase (const char* text, const char* token) noexcept
    {
        if (text == nullptr || token == nullptr)
            return false;

        while (*text != 0 && *token != 0)
        {
            if (toLowerAscii (*text++) != toLowerAscii (*token++))
                return false;
        }

        return *text == 0 && *token == 0;
    }

    // Map a trailing parameter segment to its Param kind (case-insensitive).
    inline Param classifyParam (const char* param) noexcept
    {
        if (equalsIgnoreCase (param, kParamU))    return Param::U;
        if (equalsIgnoreCase (param, kParamV))    return Param::V;
        if (equalsIgnoreCase (param, kParamLine)) return Param::Line;
        if (equalsIgnoreCase (param, kParamOn))   return Param::On;
        if (equalsIgnoreCase (param, kParamOff))  return Param::Off;
        return Param::None;
    }

    // Result of parsing an address. valid == false means "not a /cs/ message
    // we understand"; the caller should ignore it.
    struct Address
    {
        bool        valid = false;
        int         row   = 0;
        int         col   = 0;
        int         finger = 0;
        const char* param = nullptr; // points into the input string (the
                                     // trailing segment, NUL-terminated)
    };

    /*
        Parse "/cs/<row>/<col>/finger<n>/<param>" out of a raw, NUL-terminated
        UTF-8 address string, without allocating.

        maxSources bounds the accepted source id ([0, maxSources)). Returns
        Address{valid=false} on any mismatch.

        On success, .param points into `addr` at the trailing segment and is
        valid for the lifetime of `addr`.
    */
    inline Address parseAddress (const char* addr, int maxSources) noexcept
    {
        Address out;

        // Prefix: '/', 'c', 's', '/' (c/s case-insensitive). Advance only
        // after the current byte matches, so "", "/", "/c" and "/cs" never
        // dereference beyond their own NUL terminator.
        if (addr == nullptr)
            return out;

        const char* p = addr;
        if (*p != '/') return out;
        ++p;
        if (toLowerAscii(*p) != 'c') return out;
        ++p;
        if (toLowerAscii(*p) != 's') return out;
        ++p;
        if (*p != '/') return out;
        ++p;

        // Row: exactly one letter A..Z (case-insensitive). Requiring the slash
        // immediately after the letter prevents malformed tokens such as A1
        // from silently aliasing Zone A.
        char rowChar = *p;
        if (rowChar >= 'a' && rowChar <= 'z')
            rowChar = (char) (rowChar - ('a' - 'A'));
        if (rowChar < kRowFirst || rowChar > kRowLast || p[1] != '/')
            return out;
        const int row = (int) (rowChar - kRowFirst);
        p += 2;

        // Column: one or more decimal digits.
        if (maxSources <= 0)
            return out;

        int maxDigits = 1;
        for (int upper = maxSources - 1; upper >= 10; upper /= 10)
            ++maxDigits;

        bool hasCol = false;
        int  digits = 0;
        int  col    = 0;
        while (*p >= '0' && *p <= '9')
        {
            hasCol = true;
            if (++digits > maxDigits)
                return out;
            const int digit = *p - '0';

            // Return at the first impossible digit. Besides avoiding integer
            // overflow this bounds work for a hostile address containing tens
            // of thousands of digits (a UDP datagram can be almost 64 KiB).
            if (digit > maxSources - 1
                || col > (maxSources - 1 - digit) / 10)
                return out;

            col = col * 10 + digit;
            ++p;
        }

        if (! hasCol || *p != '/')
            return out;
        if (col < 0 || col >= maxSources)
            return out;
        ++p;

        // Finger: exact lower-case "finger" plus one decimal digit 0..9.
        // Check each byte through the terminator rather than indexing six
        // bytes unconditionally. This keeps every truncated prefix within the
        // logical bounds of its NUL-terminated string.
        for (std::size_t i = 0; i < kFingerPrefixLen; ++i)
        {
            if (*p == 0 || *p != kFingerPrefix[i])
                return out;
            ++p;
        }

        if (*p < '0' || *p > '9')
            return out;
        const int finger = *p - '0';
        ++p;
        if (*p != '/')
            return out;

        const char* const param = p + 1;
        if (classifyParam(param) == Param::None)
            return out;

        out.valid = true;
        out.row   = row;
        out.col   = col;
        out.finger = finger;
        out.param = param; // trailing segment (NUL-terminated within addr)
        return out;
    }
}
