# GenMark 
GenMark is a cryptographically verifiable watermarking framework for tagging and detection of AI generated multimedia content.
It allows service providers to embed authenticity proofs into media at generation time and verify integrity later. 

**GenMark is currently under development, stable release will be announced in the future.**

GenMark is designed for: 
- AI generated Video 
- AI generated Images 
- Public verification systems 
- Social Media

# What GenMark Provides 

- Cryptographic signing of generated media.
- Verifiable authenticity and origin.
- Cross-platform support
- Deterministic verification logic. 

# GenMark does not automatically provide:
- Legal Accountability
- Non repudiation in courts.
- Moral Legitimacy

# Project Structure 

```
bindings/ - Contains python and js bindings (js supported via wasm)
cli/ - cli wrapper for verrifier logic
core/ - core logic for both modules
docs/ - API reference
``` 

# Supported Environments 
- Native C++ 
- Python for directly embedding into generation pipeline
- WebAssembly for browser side deployments, supported via [emscripten](https://github.com/emscripten-core/emscripten)

# Build 
GenMark builds two standalone binaries `genmark-creator` and `genmark-verifier` both are statically linked with libsodium.
FFmpeg is required at runtime for media decoding but **not linked into the binary.**

# Requirements 
GenMark requires: 
```
C++ 20 or later
Cmake => 3.2.0 
Make or Ninja 
libsodium (static build recommended)
ffmpeg 6.x or higher 
```

# Documentation 
Full documentation including, 

- API Design Goals 
- Protocol details 
- Payload format 
is available in `docs/` 

