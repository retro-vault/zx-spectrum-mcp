/*
 * miniz_export.h
 *
 * Upstream miniz generates this header with CMake to control symbol
 * visibility for shared library builds. This project links miniz
 * statically into a single executable, so every export macro expands to
 * nothing.
 *
 * This file is a local addition, not part of upstream miniz. See
 * UPSTREAM.md.
 */

#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#endif /* MINIZ_EXPORT_H */
