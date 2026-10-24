/*
 * wp/wptime32.h
 *
 * FPAC project
 *
 * Conversion of a White Pages file written with a 32-bit time_t.
 */

#ifndef WPTIME32_H
#define WPTIME32_H

/*
 * If 'path' is a White Pages file whose records were written with a 32-bit
 * time_t (an old 32-bit system), convert it in place to the layout of this
 * build. The old file is kept as '<path>.time32-<date>'.
 * Returns 1 if the file was converted, 0 if there was nothing to do (missing
 * file, already in the right layout, or not a White Pages file at all), and
 * -1 if the file needs converting but the conversion failed; the file is
 * then left untouched and must not be overwritten.
 */
int wp_convert_time32(const char *path);

#endif
