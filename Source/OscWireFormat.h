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
        <row>       a single letter A..Z (case-insensitive) -> row index 0..25.
                    Anything after the letter up to the next '/' is ignored
                    (e.g. "/cs/A/..." and "/cs/A1/..." both yield row 0).
        <col>       one or more decimal digits -> column index. Must be in
                    [0, MAX_COLS). The digits must be followed by '/'.
        finger<n>   an opaque segment (the finger id). Its contents are not
                    inspected; everything up to the next '/' is skipped.
        <param>     the trailing segment, one of (case-insensitive):
                        "on"   -> note on/off    (arg: int/float, !=0 => on)
                        "off"  -> note off       (no arg required)
                        "v"    -> vertical value (arg: float 0..1)
                        "line" -> horizontal val (arg: float 0..127)

    This header only parses the *address* (row / col / param). Argument
    decoding and clamping stay in the caller, because they depend on the
    OSC argument types (OSCMessage), not on the address string.

    DESIGN NOTES
    ------------
    - Header-only / inline so it can be shared without touching the build.
    - The parser takes a raw, NUL-terminated UTF-8 C string so callers can
      parse straight off juce::OSCAddressPattern's underlying storage without
      allocating a juce::String per message on the OSC thread (see B26).
    - The matching logic here is byte-for-byte equivalent to the original
      hand-rolled parser in OscBridge::oscMessageReceived; this is a pure
      refactor (B16) and is intentionally behaviour-preserving.
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
    inline constexpr char kParamV[]    = "v";
    inline constexpr char kParamLine[] = "line";

    // Parsed parameter kind. Unknown trailing tokens map to None.
    enum class Param { None, On, Off, V, Line };

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
        const char* param = nullptr; // points into the input string (the
                                     // trailing segment, NUL-terminated)
    };

    /*
        Parse "/cs/<row>/<col>/finger<n>/<param>" out of a raw, NUL-terminated
        UTF-8 address string, without allocating.

        maxCols bounds the accepted column index ([0, maxCols)); callers pass
        their seat-grid width. Returns Address{valid=false} on any mismatch,
        exactly where the original OscBridge parser would have bailed out.

        On success, .param points into `addr` at the trailing segment and is
        valid for the lifetime of `addr`.
    */
    inline Address parseAddress (const char* addr, int maxCols) noexcept
    {
        Address out;

        // Prefix: '/', 'c', 's', '/'  (c/s case-insensitive, matching original)
        if (addr == nullptr
            || addr[0] != '/'
            || toLowerAscii (addr[1]) != 'c'
            || toLowerAscii (addr[2]) != 's'
            || addr[3] != '/')
            return out;

        const char* p = addr + kPrefixLen;

        // Row: a single letter A..Z (case-insensitive).
        char rowChar = *p;
        if (rowChar >= 'a' && rowChar <= 'z')
            rowChar = (char) (rowChar - ('a' - 'A'));
        if (rowChar < kRowFirst || rowChar > kRowLast)
            return out;
        const int row = (int) (rowChar - kRowFirst);

        // Skip the rest of the row segment up to the next '/'.
        while (*p != 0 && *p != '/')
            ++p;
        if (*p != '/')
            return out;
        ++p;

        // Column: one or more decimal digits.
        bool hasCol = false;
        int  col    = 0;
        while (*p >= '0' && *p <= '9')
        {
            hasCol = true;
            // Once col reaches maxCols it will be rejected below regardless of its
            // exact value, so clamp it there instead of continuing to grow it. A
            // pathological all-digit column would otherwise signed-overflow int
            // (UB). Valid columns (< maxCols) accumulate exactly as before.
            if (col < maxCols)
                col = col * 10 + (*p - '0');
            if (col > maxCols)
                col = maxCols;
            ++p;
        }

        if (! hasCol || *p != '/')
            return out;
        if (col < 0 || col >= maxCols)
            return out;
        ++p;

        // Skip the finger<n> segment up to the next '/'.
        while (*p != 0 && *p != '/')
            ++p;
        if (*p != '/')
            return out;

        out.valid = true;
        out.row   = row;
        out.col   = col;
        out.param = p + 1; // trailing segment (NUL-terminated within addr)
        return out;
    }
}
