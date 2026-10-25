---
kind: chore
---
Adopt C++23 idioms across the firmware

## Release Note
An internal cleanup with no intended change in behaviour.

- Return `std::optional` or `std::expected` instead of `bool` plus an out parameter: config getters, JSON accessors, the enum, SemVer and split parsers, JSON API responses, GPIO conversion and the OTA helpers
- `Convert::To*` become constexpr wrappers over `std::from_chars`; the `From*` functions and `DigitCounter` are removed, and `HexUtils` and `Checksum` take `std::string_view` and `std::span`
- Captive portal parameters report a missing and an invalid value separately, and `urlDecode` bounds-checks percent escapes by length
- Add `util/FunctionRef.h`, a stand-in for C++26 `std::function_ref`, for non-owning callbacks
- Use `std::to_underlying`, `using` aliases and `consteval` where they apply, and drop `default:` from exhaustive switches so a new enumerator warns
