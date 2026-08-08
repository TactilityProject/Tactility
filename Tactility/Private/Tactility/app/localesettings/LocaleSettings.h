#pragma once

// Intentionally empty: LocaleSettings.cpp has no external callers (verified via repo-wide
// grep during its thread-per-app conversion), so it no longer exposes a start() wrapper. This
// header is kept as a placeholder in case that changes; nothing currently includes it.