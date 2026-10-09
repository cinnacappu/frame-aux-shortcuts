# Third-party licenses

frame-aux-shortcuts is released under the MIT License (see [LICENSE](LICENSE)). This file lists the third-party code it contains and what it uses at run time.

## Bundled

- `vendor/frame-updater/`, `vendor/frame-apps/` and some files in `src/` (from [frame-perf-overlay](https://github.com/sasaken1102r/frame-perf-overlay) and [frame-mic-tuner](https://github.com/sasaken1102r/frame-mic-tuner)) are the author's own code under the same MIT License, not third-party code. So are the launcher icons in `contrib/icons/` and the images in `docs/images/`.
- `third_party/openvr/openvr.h`: OpenVR SDK v2.15.6 header by Valve Corporation (https://github.com/ValveSoftware/openvr), compiled in, BSD-3-Clause:

```
Copyright (c) 2015, Valve Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

- Line icons (`src/icons.cpp`): drawn from coordinates in the code; some follow Feather (https://github.com/feathericons/feather; the camera uses the same coordinates) and Lucide (https://github.com/lucide-icons/lucide; the sun). Their licenses, to be safe:

```
The MIT License (MIT)

Copyright (c) 2013-2017 Cole Bemis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

----

ISC License

Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of Feather (MIT). All other copyright (c) for Lucide are held by Lucide Contributors 2022.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

## Used at run time, not bundled

The programs link to, run or read these copies already on the Steam Frame (checked with `readelf -d` / `ldd` on the release binaries, SteamOS of 2026-10-07):

| Component | Use | License |
|---|---|---|
| `libopenvr_api.so` (from SteamVR) | linked | BSD-3-Clause |
| cairo 1.18.0, FreeType 2.13.2 | linked; drawing and fonts | LGPL-2.1 or MPL-1.1; FTL or GPL-2.0 |
| Vulkan loader 1.4.309 | linked | Apache-2.0 |
| glibc 2.39, GCC runtime (`libstdc++`, `libgcc_s`) | linked | LGPL-2.1-or-later; GPL-3.0 with the GCC Runtime Library Exception |
| Noto Sans CJK | font read at run time | SIL OFL 1.1 |
| `wpctl` (WirePlumber), `pw-play` (PipeWire) | run for mic mute and sounds | MIT |
| `systemctl`, `systemd-run`; curl, tar, `sha256sum`, Python 3 | run for autostart and by the updater | LGPL-2.1+; curl, GPL-3.0+, PSF |
| SteamVR's dashboard sounds (`.wav`) | default feedback sounds, played from SteamVR's folder | © Valve; not redistributed |

They are used as shared libraries and programs from the Steam Frame and can be replaced with any compatible version. The text of the LGPL 2.1 (cairo, glibc) is in [`licenses/LGPL-2.1.txt`](licenses/LGPL-2.1.txt). Portions of this software are based in part on the work of the FreeType Team (https://freetype.org).
