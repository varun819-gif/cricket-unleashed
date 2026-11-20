#include "cricket/controller/QrCode.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace cricket::ctl {

namespace {

// Error-correction level M, versions 1..10 (index 0 unused).
const int kEccPerBlock[11] = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
const int kNumBlocks[11] = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
constexpr int kFormatBitsM = 0;  // ECC level M = 00 in the format information

int RawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

int DataCodewords(int ver) { return RawDataModules(ver) / 8 - kEccPerBlock[ver] * kNumBlocks[ver]; }

uint8_t GfMul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

std::vector<uint8_t> RsDivisor(int degree) {
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = GfMul(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = GfMul(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> RsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i) result[i] ^= GfMul(divisor[i], factor);
    }
    return result;
}

std::vector<int> AlignmentPositions(int ver) {
    if (ver == 1) return {};
    const int numAlign = ver / 7 + 2;
    const int size = ver * 4 + 17;
    const int step = (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    std::vector<int> result{6};
    for (int i = 0, pos = size - 7; i < numAlign - 1; ++i, pos -= step) result.insert(result.begin() + 1, pos);
    return result;
}

bool Bit(long x, int i) { return ((x >> i) & 1) != 0; }

}  // namespace

std::optional<QrCode> QrCode::Encode(const std::string& bytes, int mask) {
    if (mask < -1 || mask > 7) return std::nullopt;
    int ver = 1;
    for (; ver <= 10; ++ver) {
        const int countBits = ver <= 9 ? 8 : 16;
        const int needBits = 4 + countBits + static_cast<int>(bytes.size()) * 8;
        if (needBits <= DataCodewords(ver) * 8) break;
    }
    if (ver > 10) return std::nullopt;

    // Bit stream: mode (byte = 0100), length, data, terminator, padding.
    std::vector<bool> bits;
    auto put = [&](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) bits.push_back(((v >> i) & 1) != 0);
    };
    put(0x4, 4);
    put(static_cast<uint32_t>(bytes.size()), ver <= 9 ? 8 : 16);
    for (unsigned char c : bytes) put(c, 8);
    const size_t capacity = static_cast<size_t>(DataCodewords(ver)) * 8;
    put(0, static_cast<int>(std::min<size_t>(4, capacity - bits.size())));
    put(0, static_cast<int>((8 - bits.size() % 8) % 8));
    for (uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11) put(pad, 8);
    std::vector<uint8_t> data(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) data[i >> 3] |= static_cast<uint8_t>(bits[i] << (7 - (i & 7)));

    // Error correction per block, then interleave.
    const int numBlocks = kNumBlocks[ver];
    const int blockEccLen = kEccPerBlock[ver];
    const int rawCodewords = RawDataModules(ver) / 8;
    const int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    const int shortBlockLen = rawCodewords / numBlocks;
    const std::vector<uint8_t> divisor = RsDivisor(blockEccLen);
    std::vector<std::vector<uint8_t>> blocks;
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        const int len = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> dat(data.begin() + k, data.begin() + k + len);
        k += len;
        const std::vector<uint8_t> ecc = RsRemainder(dat, divisor);
        if (i < numShortBlocks) dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }
    std::vector<uint8_t> all;
    for (size_t i = 0; i < blocks[0].size(); ++i)
        for (int j = 0; j < numBlocks; ++j)
            if (i != static_cast<size_t>(shortBlockLen - blockEccLen) || j >= numShortBlocks) all.push_back(blocks[j][i]);

    QrCode q;
    q.version_ = ver;
    q.size_ = ver * 4 + 17;
    q.modules_.assign(static_cast<size_t>(q.size_) * q.size_, false);
    q.isFunction_.assign(q.modules_.size(), false);
    q.DrawFunctionPatterns();
    q.DrawCodewords(all);

    if (mask == -1) {
        long best = LONG_MAX;
        for (int m = 0; m < 8; ++m) {
            q.ApplyMask(m);
            q.DrawFormatBits(m);
            const long p = q.Penalty();
            if (p < best) {
                best = p;
                mask = m;
            }
            q.ApplyMask(m);  // undo (XOR)
        }
    }
    q.mask_ = mask;
    q.ApplyMask(mask);
    q.DrawFormatBits(mask);
    return q;
}

void QrCode::Set(int x, int y, bool dark, bool function) {
    modules_[static_cast<size_t>(y) * size_ + x] = dark;
    if (function) isFunction_[static_cast<size_t>(y) * size_ + x] = true;
}

void QrCode::DrawFunctionPatterns() {
    for (int i = 0; i < size_; ++i) {
        Set(6, i, i % 2 == 0, true);
        Set(i, 6, i % 2 == 0, true);
    }
    DrawFinder(3, 3);
    DrawFinder(size_ - 4, 3);
    DrawFinder(3, size_ - 4);
    const std::vector<int> pos = AlignmentPositions(version_);
    const int n = static_cast<int>(pos.size());
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) DrawAlignment(pos[i], pos[j]);
    DrawFormatBits(0);  // placeholder, overwritten later
    DrawVersion();
}

void QrCode::DrawFormatBits(int mask) {
    const int data = kFormatBitsM << 3 | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = (data << 10 | rem) ^ 0x5412;
    for (int i = 0; i <= 5; ++i) Set(8, i, Bit(bits, i), true);
    Set(8, 7, Bit(bits, 6), true);
    Set(8, 8, Bit(bits, 7), true);
    Set(7, 8, Bit(bits, 8), true);
    for (int i = 9; i < 15; ++i) Set(14 - i, 8, Bit(bits, i), true);
    for (int i = 0; i < 8; ++i) Set(size_ - 1 - i, 8, Bit(bits, i), true);
    for (int i = 8; i < 15; ++i) Set(8, size_ - 15 + i, Bit(bits, i), true);
    Set(8, size_ - 8, true, true);  // the dark module
}

void QrCode::DrawVersion() {
    if (version_ < 7) return;
    int rem = version_;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    const long bits = static_cast<long>(version_) << 12 | rem;
    for (int i = 0; i < 18; ++i) {
        const bool b = Bit(bits, i);
        const int a = size_ - 11 + i % 3, c = i / 3;
        Set(a, c, b, true);
        Set(c, a, b, true);
    }
}

void QrCode::DrawFinder(int x, int y) {
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx) {
            const int dist = std::max(std::abs(dx), std::abs(dy));
            const int xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < size_ && yy >= 0 && yy < size_) Set(xx, yy, dist != 2 && dist != 4, true);
        }
}

void QrCode::DrawAlignment(int x, int y) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) Set(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1, true);
}

void QrCode::DrawCodewords(const std::vector<uint8_t>& data) {
    size_t i = 0;
    for (int right = size_ - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size_; ++vert)
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? size_ - 1 - vert : vert;
                if (!isFunction_[static_cast<size_t>(y) * size_ + x] && i < data.size() * 8) {
                    modules_[static_cast<size_t>(y) * size_ + x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                    ++i;
                }
            }
    }
}

void QrCode::ApplyMask(int mask) {
    for (int y = 0; y < size_; ++y)
        for (int x = 0; x < size_; ++x) {
            bool invert;
            switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            const size_t k = static_cast<size_t>(y) * size_ + x;
            if (!isFunction_[k] && invert) modules_[k] = !modules_[k];
        }
}

long QrCode::Penalty() const {
    long result = 0;
    const int n1 = 3, n2 = 3, n3 = 40, n4 = 10;
    auto addHistory = [&](int run, std::vector<int>& h) {
        if (h[0] == 0) run += size_;  // light border before the first run
        std::rotate(h.rbegin(), h.rbegin() + 1, h.rend());
        h[0] = run;
    };
    auto countPatterns = [&](const std::vector<int>& h) {
        const int n = h[1];
        const bool core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
        return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
    };
    auto terminate = [&](bool color, int run, std::vector<int>& h) {
        if (color) {
            addHistory(run, h);
            run = 0;
        }
        run += size_;
        addHistory(run, h);
        return countPatterns(h);
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < size_; ++a) {
            bool runColor = false;
            int run = 0;
            std::vector<int> h(7, 0);
            for (int b = 0; b < size_; ++b) {
                const bool c = pass == 0 ? Dark(b, a) : Dark(a, b);
                if (c == runColor) {
                    ++run;
                    if (run == 5) result += n1;
                    else if (run > 5) ++result;
                } else {
                    addHistory(run, h);
                    if (!runColor) result += countPatterns(h) * n3;
                    runColor = c;
                    run = 1;
                }
            }
            result += terminate(runColor, run, h) * n3;
        }
    }
    for (int y = 0; y < size_ - 1; ++y)
        for (int x = 0; x < size_ - 1; ++x) {
            const bool c = Dark(x, y);
            if (c == Dark(x + 1, y) && c == Dark(x, y + 1) && c == Dark(x + 1, y + 1)) result += n2;
        }
    long dark = 0;
    for (bool m : modules_) dark += m ? 1 : 0;
    const long total = static_cast<long>(size_) * size_;
    const long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += k * n4;
    return result;
}

std::string QrCode::ToAscii() const {
    std::string o;
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) o += Dark(x, y) ? '#' : '.';
        o += '\n';
    }
    return o;
}

std::string QrCode::ToTerminal() const {
    // Light background, dark modules: top/bottom halves per character.
    const int q = 4;
    auto dark = [&](int x, int y) { return x >= 0 && y >= 0 && x < size_ && y < size_ && Dark(x, y); };
    std::string o;
    for (int y = -q; y < size_ + q; y += 2) {
        for (int x = -q; x < size_ + q; ++x) {
            const bool top = dark(x, y), bottom = dark(x, y + 1);
            // Printed as light-on-dark-terminal friendly: full block = light.
            if (!top && !bottom) o += "\xE2\x96\x88";       // █
            else if (top && !bottom) o += "\xE2\x96\x84";   // ▄
            else if (!top && bottom) o += "\xE2\x96\x80";   // ▀
            else o += ' ';
        }
        o += '\n';
    }
    return o;
}

std::string QrCode::ToSvg(int m) const {
    const int q = 4, dim = (size_ + 2 * q) * m;
    std::string o = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(dim) + " " +
                    std::to_string(dim) + "\" width=\"" + std::to_string(dim) + "\" height=\"" + std::to_string(dim) +
                    "\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path fill=\"#000\" d=\"";
    for (int y = 0; y < size_; ++y)
        for (int x = 0; x < size_; ++x)
            if (Dark(x, y))
                o += "M" + std::to_string((x + q) * m) + "," + std::to_string((y + q) * m) + "h" + std::to_string(m) +
                     "v" + std::to_string(m) + "h-" + std::to_string(m) + "z";
    o += "\"/></svg>";
    return o;
}

}  // namespace cricket::ctl
