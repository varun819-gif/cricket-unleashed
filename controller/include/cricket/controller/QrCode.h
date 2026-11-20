// QR code generator (ISO/IEC 18004): byte mode, error-correction level M
// (about 15% of the code can be damaged and still read), versions 1-10
// (up to 213 bytes - plenty for a pairing link). Written from the public
// standard; verified against an independent reference implementation.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cricket::ctl {

class QrCode {
public:
    // Smallest version that fits. mask = -1 picks the best mask by the
    // standard's penalty rules; 0-7 forces one (used by tests).
    static std::optional<QrCode> Encode(const std::string& bytes, int mask = -1);

    int Size() const { return size_; }
    int Version() const { return version_; }
    int Mask() const { return mask_; }
    bool Dark(int x, int y) const { return modules_[static_cast<size_t>(y) * size_ + x]; }

    // Two rows per text line using half-block characters, with the
    // required 4-module quiet zone. Readable by phone cameras from a terminal.
    std::string ToTerminal() const;
    // Standalone SVG (for the TV/PC screen page).
    std::string ToSvg(int moduleSize = 8) const;
    // '#' / '.' rows, for tests.
    std::string ToAscii() const;

private:
    void Set(int x, int y, bool dark, bool function);
    void DrawFunctionPatterns();
    void DrawFormatBits(int mask);
    void DrawVersion();
    void DrawFinder(int x, int y);
    void DrawAlignment(int x, int y);
    void DrawCodewords(const std::vector<uint8_t>& data);
    void ApplyMask(int mask);
    long Penalty() const;

    int version_ = 1;
    int size_ = 21;
    int mask_ = 0;
    std::vector<bool> modules_;
    std::vector<bool> isFunction_;
};

}  // namespace cricket::ctl
