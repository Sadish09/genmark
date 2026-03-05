#pragma once

#include "types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace genmark {

// ─── Frame geometry ───────────────────────────────────────────────────────────
//
// All transforms operate on a single luma (Y) plane laid out as a contiguous,
// row-major byte buffer.  Chroma planes are handled by the caller — we do not
// touch them here.  This keeps the transform layer codec-agnostic: the caller
// extracts whatever plane it needs before calling forward(), and writes it back
// afterwards.
//
// Invariant: media.size() == geometry.stride * geometry.height
//
struct FrameGeometry {
    int width;   // visible pixel columns
    int height;  // visible pixel rows
    int stride;  // bytes per row (>= width; accounts for decoder padding)
};

// ─── Coefficient access ───────────────────────────────────────────────────────
//
// After a forward() call the buffer holds transform coefficients stored as
// doubles repacked into the same byte layout via a side buffer owned by the
// transform object.  Rather than exposing raw pointers, the interface provides
// a typed accessor so watermark strategies never need to know internal layout.
//
// CoeffRef: read/write reference to one transform coefficient.
// CoeffPos: logical (block_x, block_y, u, v) address within the coefficient
//           space.  Interpretation depends on the concrete transform.
//
struct CoeffPos {
    int block_col; // horizontal block index
    int block_row; // vertical block index
    int u;         // frequency index (row in block)
    int v;         // frequency index (column in block)
};

// ─── Primary interface ────────────────────────────────────────────────────────

class ITransform {
public:
    virtual ~ITransform() = default;

    // Not copyable — transforms own internal coefficient buffers.
    ITransform(const ITransform&)            = delete;
    ITransform& operator=(const ITransform&) = delete;
    ITransform(ITransform&&)                 = default;
    ITransform& operator=(ITransform&&)      = default;

    // ── Core pipeline ─────────────────────────────────────────────────────

    // forward(): decode spatial pixels → frequency coefficients.
    //
    // On success the transform object retains an internal coefficient buffer.
    // The caller may then read/write coefficients via coeff() before calling
    // inverse().
    //
    // media must satisfy: media.size() == geo.stride * geo.height
    // media values are treated as unsigned 8-bit luma samples [0, 255].
    //
    // Returns Status::InvalidArgument if geometry is incompatible with this
    // transform (e.g. dimensions not a multiple of the block size).
    //
    [[nodiscard]] virtual Status forward(std::span<const std::byte> media,
                                         const FrameGeometry&       geo) = 0;

    // inverse(): frequency coefficients → spatial pixels.
    //
    // Writes the reconstructed luma plane back into `media`.  Coefficients
    // are clamped to [0, 255] and rounded to the nearest integer.
    //
    // Must be called after a successful forward().
    //
    [[nodiscard]] virtual Status inverse(std::span<std::byte>  media,
                                         const FrameGeometry&  geo) = 0;

    // ── Coefficient access (valid between forward() and inverse()) ─────────

    // Returns a mutable reference to coefficient at position p.
    // Behaviour is undefined if p is out of range or forward() has not been
    // called successfully.
    [[nodiscard]] virtual double& coeff(const CoeffPos& p)       = 0;
    [[nodiscard]] virtual double  coeff(const CoeffPos& p) const = 0;

    // Total number of blocks in each dimension.
    [[nodiscard]] virtual int block_cols() const noexcept = 0;
    [[nodiscard]] virtual int block_rows() const noexcept = 0;

    // Frequency dimensions of one block (e.g. 8×8 for DCT, variable for DWT).
    [[nodiscard]] virtual int freq_u() const noexcept = 0;
    [[nodiscard]] virtual int freq_v() const noexcept = 0;

    // ── Metadata ──────────────────────────────────────────────────────────

    [[nodiscard]] virtual MediaType        media_type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view transform_id() const noexcept = 0;

protected:
    ITransform() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────
//
// Returns a DCT-II / IDCT-III transform operating on 8×8 luma blocks.
// This is the standard JPEG baseline transform.
//
// Thread-safe: multiple instances may be used concurrently.
// Each instance owns its own coefficient buffer — no shared mutable state.
//
[[nodiscard]] std::unique_ptr<ITransform> make_dct_transform();

// Future:
//   make_dwt_transform()   — for DWT-based embedding (JPEG2000 style)
//   make_fft_transform()   — for audio tagging

} // namespace genmark