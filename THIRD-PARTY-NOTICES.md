# Third-party notices

terminal-jira itself is GPL-3.0-or-later (see [LICENSE](LICENSE)). It is built on the
libraries below, all of which are free software under permissive,
GPL-compatible terms. Their licences require their copyright notices to travel
with any copy of the software, which is what this file is for.

| Library | Version | Licence | Linkage |
| --- | --- | --- | --- |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | v5.0.0 | MIT | static |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | header-only |
| [libcurl](https://curl.se/) | system | curl (MIT/X derivative) | shared |

FTXUI and nlohmann/json are fetched at configure time by CMake; libcurl comes
from the distribution.

A note on OpenSSL: most distributions build libcurl against OpenSSL 3, which is
Apache-2.0. That is compatible with GPLv3 but *not* with GPLv2, which is why
this project is GPLv3 and not GPLv2. A libcurl built against GnuTLS (LGPL) works
equally well. Everything else libcurl pulls in — nghttp2, brotli, zstd, libpsl,
libidn2, libssh, OpenLDAP, Kerberos — is MIT, BSD or LGPL, and is linked
dynamically.

Only what ships in the binary is listed here. CMake (BSD-3-Clause), GCC/Clang,
Python 3 and `script(1)` are used to build and test, and impose nothing on the
result.

---

## FTXUI

Copyright (c) 2019 Arthur Sonzogni.

Parts of `screen/string.cpp` derive from the Unicode Character Database and from
Markus Kuhn's `wcwidth.c` (2007-05-26), both used under permissive terms and
redistributed by FTXUI under the licence below.

```
The MIT License

Copyright (c) 2019 Arthur Sonzogni.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## nlohmann/json

Copyright (c) 2013-2022 Niels Lohmann. The bundled Hedley header is additionally
copyright 2016-2021 Evan Nemerson and is distributed under the same licence.

```
MIT License

Copyright (c) 2013-2022 Niels Lohmann

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
```

The repository also carries Apache-2.0, BSD-3-Clause and GPL-3.0-only files
under `tests/` and `tools/`. None of them are compiled into terminal-jira; only the
MIT-licensed headers in `include/` are used.

## libcurl

Copyright (c) 1996 - 2023, Daniel Stenberg, <daniel@haxx.se>, and many
contributors.

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2023, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.
```
