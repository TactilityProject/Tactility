# License Grant

## Definitions

"external apps" or "external applications" refers to applications that are built with the TactilitySDK.
These applications are not part of the Tactility operating system's main firmware.

"end-users" refers to people who install and/or use Tactility software on their devices.

## Licensing Intent

The main intention is to make sure forks of the operating system stay open source,
while external applications can have both open or closed source licenses.

TactilitySDK combines several subprojects and should not have source code with a GPL licenses inside.

## Past & Present

Formerly, there was a mixed usage of [GPL v3.0](Documentation/LICENSE-GPL-3.0.md) for internal subprojects
and [Apache License v2.0](Documentation/LICENSE-Apache-2.0.md) for subprojects that would be used in external apps.

For future subprojects, [LGPL v3.0](Documentation/LICENSE-LGPL-3.0.md) will be chosen for internal subprojects and
Apache License v2.0 will be chosen for header-only projects.
Existing GPL-licensed projects will retain this license, as it cannot be changed to LGPL.

The reason is that LGPL allows for logic in header files, but it comes with limitations (e.g. limit of 10 lines of code in headers).
If we write C++ wrappers for C libraries then we want them to be usable for building external apps, without such LGPL limitations.

## Overview

Below is an overview of the licenses of some of the subprojects.

| Project            | License             |
|--------------------|---------------------|
| Tactility          | GPL v3.0            |
| TactilityCore      | GPL v3.0            |
| TactilityC         | Apache License v2.0 |
| TactilityFreeRTOS  | Apache License v2.0 |
| TactilityKernel    | LGPL v3.0           |
| Tests              | GPL v3.0            |
| Devices/*          | GPL v3.0            |
| Drivers/*          | (varies)            |
| DevicetreeCompiler | Apache License v2.0 |

Subprojects and directories in this project can contain license files.

The presence of such a license file indicates that this license applies to all the files and folders that are contained by the folder at the level where the license file resides.

## Logo

The Tactility logo copyrights are owned by Ken Van Hoeylandt.

Logo usage is permitted in these scenarios:
- News, blog posts, articles and documentation that write about the official Tactility project.
- Firmwares built with unmodified source code from [the official repository](https://github.com/TactilityProject/Tactility) can be redistributed with the Tactility logo.
- Personal use for local builds that contain Tactility source code (original or modified), and aren't re-distributed online.

Logo usage is forbidden in all other scenarios unless an exception was granted by the author.
For other usages or exceptions, [contact me](https://kenvanhoeylandt.net).

Practical examples:
- A blog post about Tactility can use screenshots and the logo itself when writing about Tactility
- A developer who forked Tactility to merge new features or fixes to the official Tactility repository is allowed to make builds with the logo, as long as these builds are not re-distributed to end-users.

## Third Party Notices

Third-party licenses and copyrights are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## FAQ

- Q: Can I build closed source applications?
- A: Yes, external apps can be closed source. All subprojects with an Apache License or LGPL license can be used in this manner. If you fork the project and make an internal app, it must be redistributed under the GPL v3.0 license.
