# Embedded CA bundle

The native HTTPS client embeds a Mozilla-derived public CA bundle into
`libjsapi_hyperhttp.so`. This avoids relying on the X5 firmware having a
populated `/etc/ssl/certs` directory.

- Upstream source: Composer CA Bundle's `res/cacert.pem` at commit `024ad1476500f7593a6c08061b1273fd44f35158`
- Bundle date: 2026-10-20
- Git blob object ID: `c5818b24d021b6244cdc0ee865f997acd627f4fc` (verified with `git hash-object`)
- License: Mozilla Public License 2.0 (MPL-2.0), inherited from Mozilla's
  certificate source.
- Approximate payload: 195 KB PEM; final shared-library size increase is
  expected to be around 0.2 MB, depending on compiler/linker output.

The native build downloads the bundle and verifies the exact SHA-256 before
generating a temporary C source file. The generated source is compiled into
the shared library and is not committed to the repository. A changed upstream
bundle fails the blob-ID check until the source revision and expected object ID are deliberately
reviewed and updated.

At runtime, libcurl receives the embedded PEM through
`CURLOPT_CAINFO_BLOB`. TLS peer and hostname verification remain enabled.
No system CA path is required, and certificate verification is never disabled.

The bundle can become outdated over time; update it intentionally with a
reviewed upstream version and checksum as part of a future application build.
