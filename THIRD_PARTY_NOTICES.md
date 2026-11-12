# Third-party notices

## GoMud

GoMud was inspected as a mechanics and world-design reference during VolaMUD
development. The source snapshot is GoMudEngine/GoMud commit
`b3f55cc236ea67ac8827a62c68de7980474f0757`, obtained on 2026-11-11. VolaMUD
does not ship GoMud source, its default-world data, a Go runtime, web server
or JavaScript scripting. Its current city, procedural world and code are
original TinyBBS work. This notice keeps the reference provenance and the
upstream MIT licence together for any future selected port work.

Copyright (c) 2026 Dylan Squires

MIT License

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

## libopenmpt

The browser music player plays tracker modules with libopenmpt 0.8.10, the
official WebAssembly build (`libopenmpt-0.8.10+release.dev.js.tar.gz` from
https://lib.openmpt.org/libopenmpt/download/), unmodified. It ships on the card
as `/BBS/ASSETS/PLAYER/libopenmpt.js` and `libopenmpt.wasm`; the licences of
libopenmpt and the libraries built into it (miniz, minimp3, stb_vorbis) are in
`/BBS/ASSETS/PLAYER/LICENSES`. `player.js` next to them is TinyBBS code.

Copyright (c) 2004-2026, OpenMPT Project Developers and Contributors
Copyright (c) 1997-2003, Olivier Lapicque
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenMPT project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Tracker modules

The starter set in the Tracker Music area (`tools/fetch-modules.py`) comes
from The Mod Archive, https://modarchive.org/ . Its upload terms (v2.2,
section 1.2) let anyone redistribute a module that "has stayed in its
original, unmodified, unbundled form". The modules are copied byte for byte,
one file each, and each one's Mod Archive number is in the area's FILES.BBS.

## jsSID

SID tunes play with jsSID by Joe Hohertz (https://github.com/jhohertz/jsSID,
branch `develop`), with its reSID engine. The files `stream.js`,
`jssid.core.js`, `jssid.resid.js`, `jssid.mos6510.js` and `jssid.sidplayer.js`
ship unmodified in `/BBS/ASSETS/PLAYER`, as their own separate files. jsSID is
licensed under the GPL version 2; `stream.js` is MIT. The full licence text
is `/BBS/ASSETS/PLAYER/LICENSES/jsSID.LICENSE.md`, and the files on the card
are the source. They are not part of the TinyBBS firmware, which stays MIT:
`player.js` only loads them in the browser next to it. jsSID's own audio
layer (pico.js) is not shipped; `player.js` feeds the browser itself.

reSID unpacks its waveform tables with JSXCompressor (`jsxcompressor.min.js`,
from the same jsSID repository, unmodified), copyright 2008-2012 Matthias
Ehmann, Michael Gerhaeuser, Carsten Miller, Bianca Valentin, Alfred
Wassermann and Peter Wilfahrt, dual licensed under the Apache License 2.0 or
the LGPL 3. Its licence header is kept in the file.

## SID tunes

The C64 SID Music area (`tools/fetch-sids.py`) holds tunes from the High
Voltage SID Collection, https://hvsc.c64.org/ . HVSC collects tunes released
into the public domain, under "the same principles of free and unrestricted
distribution as public domain software" (HVSC FAQ). Each file is copied byte
for byte; its HVSC path is in the area's FILES.BBS.

