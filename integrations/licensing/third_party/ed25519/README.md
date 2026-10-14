# Vendored: orlp/ed25519

Source: https://github.com/orlp/ed25519 (commit fetched from `master` on 2026-10-14, files unmodified).
Licence: zlib (see `LICENSE.txt`). Compatible with closed-source commercial use; attribution in product
documentation is appreciated and is given in `docs/THIRD-PARTY.md` style notices (see `integrations/licensing/NOTICE`).

Contents: the portable C implementation of Ed25519 (key generation from a seed, signing, verification).
`sha512.c` inside is from LibTomCrypt (public domain: "free for all purposes").

Only `verify.c` plus its dependencies is needed at run time in the product. `sign.c` and `keypair.c` are
vendored for the unit tests (test keypair) only and must not be compiled into shipped binaries.
`seed.c`, `add_scalar.c` and `key_exchange.c` are deliberately not vendored.

Signatures and public keys are standard RFC 8032 Ed25519 (checked against Python `cryptography` in the tests).
Note: this library's private key representation is the 64-byte expanded key, not the 32-byte seed.
