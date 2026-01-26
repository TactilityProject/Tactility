# Contributing

## New features and boards

Feel free to open an [issue](https://github.com/TactilityProject/Tactility/issues/new)
to discuss ideas you have regarding the implementation of new boards or features.

Keep in mind that the internal APIs are changing rapidly. They might change considerably in a short timespan.
This means it's likely that you get merge conflicts while developing new features or boards.

## Fixing things

The general take here is that minor changes are not accepted at this stage.
I don't want to spend my time reviewing and discussing these during the current stage of the project.

Only fixes for serious issues are accepted, like:

- Bugs
- Crashes
- Security issues

Some examples of non-serious issues include:

- Code documentation changes
- Renaming code
- Typographical errors
- Fixes for compiler warnings that have a low impact on the actual application logic (e.g. only happens on simulator when calling a logging function)

## Anything that doesn't fall in the above categories?

Please [contact me](https://tactilityproject.org/#/support) first!

## Pull Requests

Pull requests should only contain a single set of changes that are related to each other.
That way, an approved set of changes will not be blocked by an unapproved set of changes.

## Licensing

All contributions to a Tactility (sub)project will be licensed under the license(s) of that project.

When third party code is used, its license must be included.
It's important that these third-party licenses are compatible with the relevant Tactility (sub)projects.

## Code Style

See [this document](CODING_STYLE.md) and [.clang-format](.clang-format).
