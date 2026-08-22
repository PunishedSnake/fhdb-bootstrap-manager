# Third-party notices

PS2 HDD Bootstrap Manager's original source code is released under the MIT
License in [`LICENSE`](LICENSE). That license does not replace the licenses of
the toolchain, libraries, embedded IOP modules, fonts, or other third-party
works used to build the ELF.

## PS2SDK / PS2DEV

The manager is built with PS2SDK and statically links PS2SDK libraries. Release
builds also embed PS2SDK IOP modules so the application does not depend on the
launcher's module set.

The current Graphics Synthesizer renderer converts the `msx` bitmap font from
PS2SDK's `ee/debug/src/font.c` into a texture atlas at startup. That font data
is therefore covered by the PS2SDK notice rather than the project's MIT
license.

- Project: PS2SDK
- Source: <https://github.com/ps2dev/ps2sdk>
- License: Academic Free License 2.0 (`AFL-2.0`)
- Full license text: [`PS2SDK_LICENSE.txt`](PS2SDK_LICENSE.txt)

PS2SDK source files retain their own copyright and attribution notices. This
project does not claim ownership of them or relicense them as MIT.

Release builds use the pinned `ps2dev/ps2dev:v2.0.0` toolchain image. Compiler,
runtime and system-library components supplied by that image retain their own
licenses; consult the corresponding PS2DEV distribution for their source and
notices.

## Fonts proposed for future integration

No third-party font other than PS2SDK's `msx` font is embedded at present.
Spleen is the preferred candidate for a future optional bitmap font:

- Project: Spleen
- Author: Frederic Cambus and contributors
- Source: <https://github.com/fcambus/spleen>
- License: BSD 2-Clause (`BSD-2-Clause`)

If Spleen glyph data is added, its exact source revision and unmodified license
text must be committed beside the generated font asset. Merely mentioning the
font here is not a substitute for that requirement.

Fonts without a clear redistribution and embedding license must not be copied
into the source tree, generated C tables, release ELF, screenshots, or release
archives.
