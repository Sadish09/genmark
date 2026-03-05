#include "genmark/core/include/genmark/transform.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace genmark {

// ─── DCT-II / IDCT-III over 8×8 luma blocks ──────────────────────────────────
//
// Equations implemented
// ─────────────────────
//
// Forward DCT-II (spatial → frequency):
//
//   C(u,v) = α(u)·α(v) · (1/4) ·
//            Σ_{x=0}^{7} Σ_{y=0}^{7}  f(x,y)
//              · cos[(2x+1)·u·π/16]
//              · cos[(2y+1)·v·π/16]
//
// Inverse DCT-III (frequency → spatial):
//
//   f(x,y) = (1/4) ·
//            Σ_{u=0}^{7} Σ_{v=0}^{7}  α(u)·α(v) · C(u,v)
//              · cos[(2x+1)·u·π/16]
//              · cos[(2y+1)·v·π/16]
//
// Normalisation factor:
//
//   α(k) = 1/√2   if k == 0
//   α(k) = 1       otherwise
//
// The 1/4 pre-factor together with the α terms makes this the orthonormal
// form: applying forward then inverse is lossless (within floating-point
// precision) for any 8×8 block.
//
// Coefficient layout
// ──────────────────
// coeffs_ is a flat vector of doubles with logical shape:
//
//   [block_row][block_col][u][v]
//
// Indexed as:
//   coeffs_[((br * bcols_ + bc) * BSIZE + u) * BSIZE + v]
//
// This layout keeps one complete block contiguous in memory, which is
// cache-friendly for the per-block access pattern used by watermark strategies.
//
// Padding policy
// ──────────────
// Real video frames decoded by FFmpeg are almost never exact multiples of 8.
// H.264/H.265 macro-block alignment means the *coded* dimensions are padded,
// but the *visible* (crop) dimensions passed to us via FrameGeometry may not
// be.  We pad the right column and bottom row by replication (edge-extend)
// before the forward pass, and crop back to visible dimensions on inverse.
// This is identical to what libjpeg and most hardware encoders do.
//
// Stride awareness
// ────────────────
// FFmpeg (and most decoders) return frames with stride >= width.  The extra
// bytes are padding added for SIMD alignment and must not be transformed.
// We read pixels using the stride but write coefficients in a compact layout.
//
// Precision
// ─────────
// We use double throughout.  For a production build with performance
// constraints, float with a lookup table of sufficient precision (≥ 7 decimal
// digits) would be acceptable, but double keeps the math clean and any
// difference in watermark coefficient values between float and double exceeds
// the QIM step Δ only at absurdly small Δ values.

namespace {

constexpr int BSIZE = 8;

// ─── Compile-time cosine table ────────────────────────────────────────────────
//
// cos_lut[u][x] = cos((2x+1)·u·π / 16)
//
// Stored as constexpr so it lives in .rodata with zero runtime init cost and
// no threading hazard.  The compiler evaluates this at compile time.
//
// Note: both forward and inverse use the same cosine argument — the symmetry
// of the DCT-II / DCT-III pair means the same table serves both directions.

using CosLUT = std::array<std::array<double, BSIZE>, BSIZE>;

consteval CosLUT build_cos_lut() {
    CosLUT lut{};
    for (int u = 0; u < BSIZE; ++u)
        for (int x = 0; x < BSIZE; ++x)
            lut[u][x] = std::cos((2.0 * x + 1.0) * u * std::numbers::pi / 16.0);
    return lut;
}

constexpr CosLUT kCos = build_cos_lut();

// ─── Normalisation factor ─────────────────────────────────────────────────────

constexpr double alpha(int k) noexcept {
    return (k == 0) ? (1.0 / std::numbers::sqrt2) : 1.0;
}

// ─── Per-block transforms ─────────────────────────────────────────────────────
//
// Both functions operate in-place on a [BSIZE][BSIZE] double array.
// They are deliberately free functions (not methods) so they can be tested
// independently and potentially inlined with LTO.

void dct_block(double b[BSIZE][BSIZE]) noexcept {
    double tmp[BSIZE][BSIZE];

    for (int u = 0; u < BSIZE; ++u) {
        const double au = alpha(u);
        for (int v = 0; v < BSIZE; ++v) {
            double sum = 0.0;
            for (int x = 0; x < BSIZE; ++x) {
                const double cx = kCos[u][x];
                for (int y = 0; y < BSIZE; ++y)
                    sum += b[x][y] * cx * kCos[v][y];
            }
            tmp[u][v] = 0.25 * au * alpha(v) * sum;
        }
    }

    std::memcpy(b, tmp, sizeof(tmp));
}

void idct_block(double b[BSIZE][BSIZE]) noexcept {
    double tmp[BSIZE][BSIZE];

    for (int x = 0; x < BSIZE; ++x) {
        for (int y = 0; y < BSIZE; ++y) {
            double sum = 0.0;
            for (int u = 0; u < BSIZE; ++u) {
                const double au_cu = alpha(u) * kCos[u][x];
                for (int v = 0; v < BSIZE; ++v)
                    sum += au_cu * alpha(v) * b[u][v] * kCos[v][y];
            }
            tmp[x][y] = 0.25 * sum;
        }
    }

    std::memcpy(b, tmp, sizeof(tmp));
}

// ─── Edge-extension padding ───────────────────────────────────────────────────
//
// Fills a padded_w × padded_h working buffer from the visible frame.
// Pixels outside the visible rectangle are replicated from the nearest edge
// pixel (boundary replication / "clamp" padding).
//
// padded_w and padded_h are the next multiples of BSIZE >= visible w/h.

void fill_padded(const std::byte*      src,
                 const FrameGeometry&  geo,
                 std::vector<double>&  dst,
                 int                   padded_w,
                 int                   padded_h) noexcept
{
    for (int y = 0; y < padded_h; ++y) {
        const int sy = std::min(y, geo.height - 1);  // clamp to visible row
        for (int x = 0; x < padded_w; ++x) {
            const int sx  = std::min(x, geo.width - 1);  // clamp to visible col
            const int idx = sy * geo.stride + sx;
            dst[y * padded_w + x] = static_cast<double>(
                static_cast<unsigned char>(src[idx]));
        }
    }
}

// Round and clamp a coefficient back to [0, 255] for luma storage.
inline std::byte to_byte(double v) noexcept {
    return static_cast<std::byte>(
        static_cast<unsigned char>(
            std::clamp(std::round(v), 0.0, 255.0)));
}

// ─── Pad helpers ─────────────────────────────────────────────────────────────

constexpr int pad_to_block(int n) noexcept {
    return ((n + BSIZE - 1) / BSIZE) * BSIZE;
}

} // anonymous namespace

// ─── DCTTransform ─────────────────────────────────────────────────────────────

class DCTTransform final : public ITransform {
public:
    DCTTransform() = default;

    // ── ITransform interface ──────────────────────────────────────────────

    [[nodiscard]] Status forward(std::span<const std::byte> media,
                                 const FrameGeometry&       geo) override
    {
        if (auto s = validate(media, geo); s != Status::Ok)
            return s;

        const int pw = pad_to_block(geo.width);
        const int ph = pad_to_block(geo.height);

        bcols_ = pw / BSIZE;
        brows_ = ph / BSIZE;
        coeffs_.resize(bcols_ * brows_ * BSIZE * BSIZE);

        // Fill padded working buffer with luma values.
        padded_.resize(pw * ph);
        fill_padded(media.data(), geo, padded_, pw, ph);

        // Forward DCT each block; store in coeffs_.
        for (int br = 0; br < brows_; ++br) {
            for (int bc = 0; bc < bcols_; ++bc) {
                double b[BSIZE][BSIZE];
                load_block(b, padded_.data(), bc, br, pw);
                dct_block(b);
                store_coeffs(b, bc, br);
            }
        }

        return Status::Ok;
    }

    [[nodiscard]] Status inverse(std::span<std::byte>  media,
                                 const FrameGeometry&  geo) override
    {
        if (coeffs_.empty())
            return Status::InvalidState;  // forward() not called

        if (auto s = validate_write(media, geo); s != Status::Ok)
            return s;

        const int pw = pad_to_block(geo.width);

        for (int br = 0; br < brows_; ++br) {
            for (int bc = 0; bc < bcols_; ++bc) {
                double b[BSIZE][BSIZE];
                load_coeffs(b, bc, br);
                idct_block(b);

                // Write only the visible pixels back to media.
                // Padded columns/rows are discarded.
                for (int y = 0; y < BSIZE; ++y) {
                    const int py = br * BSIZE + y;
                    if (py >= geo.height) break;

                    for (int x = 0; x < BSIZE; ++x) {
                        const int px = bc * BSIZE + x;
                        if (px >= geo.width) break;

                        media[py * geo.stride + px] = to_byte(b[y][x]);
                    }
                }
            }
        }

        return Status::Ok;
    }

    // ── Coefficient access ────────────────────────────────────────────────

    [[nodiscard]] double& coeff(const CoeffPos& p) override {
        return coeffs_[coeff_idx(p)];
    }

    [[nodiscard]] double coeff(const CoeffPos& p) const override {
        return coeffs_[coeff_idx(p)];
    }

    [[nodiscard]] int block_cols() const noexcept override { return bcols_; }
    [[nodiscard]] int block_rows() const noexcept override { return brows_; }
    [[nodiscard]] int freq_u()     const noexcept override { return BSIZE;  }
    [[nodiscard]] int freq_v()     const noexcept override { return BSIZE;  }

    // ── Metadata ──────────────────────────────────────────────────────────

    [[nodiscard]] MediaType media_type() const noexcept override {
        return MediaType::Video;
    }

    [[nodiscard]] std::string_view transform_id() const noexcept override {
        return "dct-8x8-v1";
    }

private:
    // ── Internal state ────────────────────────────────────────────────────

    int                 bcols_   = 0;
    int                 brows_   = 0;
    std::vector<double> coeffs_;   // [brow][bcol][u][v], see layout note above
    std::vector<double> padded_;   // scratch: edge-extended luma plane

    // ── Validation ────────────────────────────────────────────────────────

    // Validates that the buffer is large enough for the geometry.
    // Does NOT require dimensions to be multiples of BSIZE — padding handles it.
    static Status validate(std::span<const std::byte> media,
                           const FrameGeometry&       geo) noexcept
    {
        if (geo.width  <= 0 || geo.height <= 0)
            return Status::InvalidArgument;
        if (geo.stride < geo.width)
            return Status::InvalidArgument;
        if (static_cast<int>(media.size()) < geo.stride * geo.height)
            return Status::InvalidArgument;
        return Status::Ok;
    }

    static Status validate_write(std::span<std::byte>  media,
                                 const FrameGeometry&  geo) noexcept
    {
        if (geo.width  <= 0 || geo.height <= 0)
            return Status::InvalidArgument;
        if (geo.stride < geo.width)
            return Status::InvalidArgument;
        if (static_cast<int>(media.size()) < geo.stride * geo.height)
            return Status::InvalidArgument;
        return Status::Ok;
    }

    // ── Block I/O ─────────────────────────────────────────────────────────

    // Load one 8×8 block from the padded working buffer into a local array.
    static void load_block(double          b[BSIZE][BSIZE],
                           const double*   padded,
                           int             bc,
                           int             br,
                           int             padded_w) noexcept
    {
        const int base_x = bc * BSIZE;
        const int base_y = br * BSIZE;
        for (int y = 0; y < BSIZE; ++y)
            for (int x = 0; x < BSIZE; ++x)
                b[y][x] = padded[(base_y + y) * padded_w + (base_x + x)];
    }

    // Store one transformed block into the flat coeffs_ buffer.
    void store_coeffs(const double b[BSIZE][BSIZE], int bc, int br) noexcept {
        const int base = (br * bcols_ + bc) * BSIZE * BSIZE;
        for (int u = 0; u < BSIZE; ++u)
            for (int v = 0; v < BSIZE; ++v)
                coeffs_[base + u * BSIZE + v] = b[u][v];
    }

    // Load one block of coefficients from coeffs_ into a local array.
    void load_coeffs(double b[BSIZE][BSIZE], int bc, int br) const noexcept {
        const int base = (br * bcols_ + bc) * BSIZE * BSIZE;
        for (int u = 0; u < BSIZE; ++u)
            for (int v = 0; v < BSIZE; ++v)
                b[u][v] = coeffs_[base + u * BSIZE + v];
    }

    // Flat index into coeffs_ for a logical CoeffPos.
    [[nodiscard]] std::size_t coeff_idx(const CoeffPos& p) const noexcept {
        assert(p.block_col >= 0 && p.block_col < bcols_);
        assert(p.block_row >= 0 && p.block_row < brows_);
        assert(p.u >= 0 && p.u < BSIZE);
        assert(p.v >= 0 && p.v < BSIZE);
        return static_cast<std::size_t>(
            (p.block_row * bcols_ + p.block_col) * BSIZE * BSIZE
             + p.u * BSIZE + p.v);
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<ITransform> make_dct_transform() {
    return std::make_unique<DCTTransform>();
}

} // namespace genmark