# GenMark

**Cryptographically verifiable watermarking for AI-generated multimedia content.**

GenMark embeds a signed, content-bound payload into the frequency domain of video and image media at generation time. The mark survives re-encoding, transcoding, format conversion, resolution changes, moderate cropping, and frame loss. 
Any downstream verifier can recover the payload and confirm both the origin and the visual integrity of the content without access to the original file.

GenMark is currently under active development. A stable release will be announced separately.

---

## What Problem Does It Solve

AI-generated video distributed across the internet passes through re-encoders, format converters, and platform processors before it reaches most viewers. 
Goal of genmark is to embed an persistent verifiable watermark that stays intact after aggressive re-encoding, compression or editing.

GenMark embeds the authenticity proof into the visual content itself, not into the container. 
The proof is the content-bound fingerprint and the cryptographic signature hidden in the frequency domain of the luma plane. 
It is present whether the file is an MP4, MKV, or WebM, whether it was downloaded from a platform or re-encoded three times.

---

## What GenMark Provides

- **Cryptographic signing.** Ed25519 signatures over a content-bound message assembled from the canonical fingerprint, a deterministic HKDF nonce, and a hash of the metadata. Signing is deterministic: the same content and key always produce the same signature.
- **Content-bound authentication.** The signature is tied to the visual content via the canonical fingerprint. Transplanting a valid payload from one video into a different video is detected at verification time.
- **Robustness through re-encoding.** The watermark is embedded in DWT low-frequency approximation subbands (LL2 and LL3) using Direct Sequence Spread Spectrum. These subbands survive aggressive lossy compression because codecs are designed to preserve them.
- **Temporal resilience.** The payload is distributed across all frames using RaptorQ fountain coding. Up to 37% of frames can be lost before recovery fails. The verifier reports a confidence score.
- **Geometric attack correction.** A synchronisation template embedded in the Fourier domain enables correction of rotation, scale change, and translation before payload extraction.
- **Graceful failure reporting.** The verifier distinguishes between "watermark not present", "watermark detected but too many frames lost for recovery", and "watermark recovered and verified".
- **C2PA compatibility.** The metadata field in the wire format is an opaque byte payload. Adopters embed a C2PA Content Credential manifest there. GenMark carries it intact and covers it with the Ed25519 signature. Trust and certification layer is outside the scope of this project.

## What GenMark Does Not Provide

- **Legal accountability or non-repudiation in courts.** That requires RFC 3161 trusted timestamping and certificate-scoped signing authority, both of which are deployment concerns outside this specification.
- **Proof that metadata claims are true.** GenMark proves the payload has not been altered since signing. It cannot prove the signer told the truth in the metadata. 
- **Resistance to neural regeneration.** Running a watermarked video through a high-quality diffusion model may destroy the embedded mark. The low-resolution LL subband embedding raises the cost significantly but does not guarantee survival.


---

## Robustness Tiers

| Tier | Attack class | Status |
|------|-------------|--------|
| 1 | Re-encode, bitrate change, container change, codec transcode | Handled |
| 2 | Heavy compression (QF >= 50), colour grading, resolution change, mild blur | Handled |
| 3 | Crop, rotation, scale, frame drop, temporal trim, speed change | Handled |
| 4 | Neural codec, diffusion model regeneration, AI upscaling | Partial |
| 5 | Oracle-guided removal via repeated verifier queries | Out of scope  |

---

## Architecture

GenMark is built as a static core library linked into two standalone binaries. FFmpeg is an external runtime dependency invoked as a subprocess, never linked.

```
genmark/
├── core/
│   ├── include/genmark/
│   │   ├── types.hpp            # Shared enums and aliases (Status, MediaType)
│   │   ├── crypto.hpp           # Cryptographic subsystem (libsodium)
│   │   ├── transform.hpp        # Transform layer interface (ITransform)
│   │   └── fingerprint.hpp      # Canonical fingerprint engine
│   └── src/
│       ├── crypto.cpp
│       ├── transform.cpp        # DWT-CDF97 implementation
│       └── fingerprint.cpp      # Scene-based DCT-pHash
├── apps/
│   ├── creator/                 # genmark_creator binary
│   └── verifier/                # genmark_verifier binary
├── bindings/
│   ├── python/                  # Python bindings
│   └── js/                      # JavaScript bindings via WebAssembly (Emscripten)
├── cli/                         # CLI wrapper for verifier logic
├── third_party/
│   └── stb_image_resize/        # Pinned Lanczos resampler for platform-deterministic pHash
├── docs/
│   ├── ARCHITECTURE.md          # Component map, pipelines, wire format
│   ├── DESIGN.md                # Design decisions and rationale
│   └── spec/
│       └── engineering_spec.md  # Full algorithm and threat model reference
└── CMakeLists.txt
```

### Component Summary

| Component | Responsibility |
|-----------|---------------|
| **Fingerprint Engine** | Scene-based frame sampling, DCT-pHash per frame, SHA-256 aggregation |
| **Crypto Subsystem** | SHA-256, HKDF-SHA256 nonce derivation, Ed25519 sign and verify, SecretKey lifecycle |
| **Transform Layer** | DWT-CDF97 three-level decomposition, perceptual masking, geometric sync template |
| **Payload Codec** | Wire format serialisation, LDPC inner code, RaptorQ outer code, DSSS embedding |
| **Apps Layer** | FFmpeg subprocess, file I/O, CLI surface |

---

## Payload Wire Format

```
Offset   Size    Field
──────   ──────  ────────────────────────────────────────────────────────
0        1       version          u8, 0x02
1        32      fingerprint      SHA-256 canonical fingerprint (F)
33       32      nonce            HKDF-derived content-bound nonce (n)
65       4       metadata_len     u32 little-endian, max 256 bytes
69       N       metadata         Arbitrary bytes — C2PA manifest goes here
69+N     64      signature        Ed25519 signature of SHA-256(F || n || meta)
──────   ──────  ────────────────────────────────────────────────────────
Minimum (no metadata): 133 bytes
Maximum (256-byte metadata): 389 bytes
```

---

## Supported Environments

| Environment | Status | Notes |
|-------------|--------|-------|
| Native C++20 | In development | Primary target |
| Python | In development | Bindings for direct pipeline integration |
| WebAssembly | In development | Browser-side verification via Emscripten |

---

## Build Requirements

```
C++20 or later
CMake >= 3.20
Make or Ninja
libsodium >= 1.0.12  (static build recommended for release)
FFmpeg >= 6.0        (runtime only, not linked)
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For a fully statically linked release binary:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DGENMARK_STATIC_SODIUM=ON
cmake --build build --parallel
```

### Running

```bash
# Embed a watermark
./build/genmark_creator \
  --key signing_key.bin \
  --input original.mp4 \
  --output watermarked.mp4 \
  --metadata '{"claim_type":"ai_generated"}'

# Verify
./build/genmark_verifier \
  --key public_key.bin \
  --input watermarked.mp4
```

---

## Cryptographic Design

All primitives are provided by libsodium, statically linked in release builds.

| Primitive | Algorithm | Purpose |
|-----------|-----------|---------|
| Content hash | SHA-256 | Fingerprint aggregation, metadata hashing |
| Nonce derivation | HKDF-SHA256 | Content-bound deterministic nonce |
| Signing | Ed25519 | Detached signature over content-bound message |
| Verification | Ed25519 | Signature and fingerprint check |
| Memory hygiene | sodium_memzero | Private key erasure on SecretKey destruction |
| Comparison | sodium_memcmp | Constant-time fingerprint equality |

The signed message is `SHA-256(F || n || SHA-256(metadata))` — a fixed 96-byte buffer regardless of metadata length. The nonce `n` is derived as `HKDF-SHA256(key=F, context="genmarkv", subkey_id=1)`, making the signature valid only for the specific visual content that produced `F`.

---

## C2PA Integration  

**API Contract is not properly defined yet; it is subject to change**

GenMark is designed as a transport layer for C2PA Content Credentials. The metadata field in the wire format accepts any byte payload. To embed a C2PA manifest:

1. Assemble a C2PA Content Credential manifest using your C2PA-enrolled certificate.
2. Pass the serialised manifest as the `--metadata` argument to `genmark_creator`.
3. GenMark embeds and signs the manifest as part of the payload.
4. Verifiers that understand C2PA can validate the credential chain; verifiers that do not still confirm the Ed25519 signature and fingerprint match.

GenMark does not parse, validate, or enforce C2PA claims. The trust hierarchy — which signers are authorised to assert which claim types — is the responsibility of the CA and the C2PA trust list.

---

## Documentation

| Document | Location | Audience |
|----------|----------|----------|
| Architecture | `docs/ARCHITECTURE.md` | Contributors, integrators |
| Design decisions | `docs/DESIGN.md` | Contributors |
| Engineering specification | `docs/spec/engineering_spec.md` | Implementers, security reviewers |
| System explainer | `docs/explainer.md` | General technical audience |
| API reference | `docs/api/` | Developers using the bindings |

---

## Project Status

GenMark is pre-release. The following components are complete or in progress: 

**All components are subject to test; Please refer to Engineering specification for threat model reference**

| Component | Status |
|-----------|--------|
| Cryptographic subsystem (`crypto.hpp` / `crypto.cpp`) | Complete |
| Transform layer interface (`ITransform`) | Complete |
| DWT-CDF97 implementation | In progress |
| Canonical fingerprint engine | In progress |
| LDPC + RaptorQ ECC layer | Planned |
| DSSS-QIM embedding | Planned |
| Geometric synchronisation template | Planned |
| Python bindings | Planned |
| WebAssembly bindings | Planned |
| Validation test suite | Planned |

---

