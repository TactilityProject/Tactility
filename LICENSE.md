# Licensing Summarized

The main intention is to make sure forks of the operating system stay open source,
while external applications can be both open or closed source.

TactilitySDK combines several subprojects and should not have GPL licenses inside.

# Past & Present

Formerly, there was a mix of GPL v3.0 for internal subprojects and Apache License v2.0 for
subprojects that would be used in external apps.

For future subprojects, LGPL (Lesser GPL) v3.0 will be chosen for internal subprojects and
Apache License v2.0 will be chosen for header-only projects.

The reasoning is that LGPL allows for logic in header files with ten or fewer lines of code
in a function. If we write C++ wrappers for C libraries then we want them to be usable for
building external apps. These wrappers might have more lines of code in the header.

# Overview

Below is an overview of the licenses of some of the subprojects.

| Project           | License                  |
|-------------------|--------------------------|
| Tactility         | GPL v3.0                 |
| TactilityCore     | GPL v3.0                 |
| TactilityC        | Apache License v2.0      |
| TactilityFreeRTOS | Apache License v2.0      |
| TactilityKernel   | LGPL v3.0                |
| Tests             | GPL v3.0                 |
| Devices/*         | GPL v3.0                 |
| Drivers           | Varies                   |

# Logo

The Tactility logo copyrights are owned by Ken Van Hoeylandt.
Firmwares built from [the original repository](https://github.com/ByteWelder/Tactility) can be redistributed with the Tactility logo.
For other usages, [contact me](https://kenvanhoeylandt.net).

# Third Party

For third party copyrights, see [COPYRIGHT.md](COPYRIGHT.md).

# FAQ

- Q: Can I build closed source applications?
- A: Yes, external apps can be closed source. All subprojects with an Apache License or LGPL license can be used in this manner. If you fork the project and make an internal app, it must be redistributed under the GPL v3.0 license.
