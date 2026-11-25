# Embedded CA bundle

The native HTTPS client embeds a Mozilla-derived public CA bundle into
`libjsapi_hyperhttp.so`. This avoids relying on the X5 firmware having a
populated `/etc/ssl/certs` directory.

- Upstream source: Composer CA Bundle's `res/cacert.pem` at commit `024ad1476500f7593a6c08061b1273fd44f35158`
- Bundle date: 2026-10-20
- SHA-256: `586f451f7a2b5b93347137c4bad87b3baf698591417653aa280c7e5ccefec3bc`
- License: Mozilla Public License 2.0 (MPL-2.0), inherited from Mozilla's
  certificate source.
- Approximate payload: 195 KB PEM; final shared-library size increase is
  expected to be around 0.2 MB, depending on compiler/linker output.

The native build downloads the bundle and verifies the exact SHA-256 before
generating a temporary C source file. The generated source is compiled into
the shared library and is not committed to the repository. A changed upstream
bundle fails the checksum check until the version and hash are deliberately
reviewed and updated.

At runtime, libcurl receives the embedded PEM through
`CURLOPT_CAINFO_BLOB`. TLS peer and hostname verification remain enabled.
No system CA path is required, and certificate verification is never disabled.

The bundle can become outdated over time; update it intentionally with a
reviewed upstream version and checksum as part of a future application build.
