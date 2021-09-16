#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum QuadSplitFlags {
        QUAD_SPLIT_RELAX                    = 1 << 0, /* Allow unbalanced quote and eat up trailing backslash. */
        QUAD_SPLIT_CUNESCAPE                = 1 << 1, /* Unescape known escape sequences. */
        QUAD_SPLIT_UNESCAPE_RELAX           = 1 << 2, /* Allow and keep unknown escape sequences, allow and keep trailing backslash. */
        QUAD_SPLIT_UNESCAPE_SEPARATORS      = 1 << 3, /* Unescape separators (those specified, or whitespace by default). */
        QUAD_SPLIT_KEEP_QUOTE               = 1 << 4, /* Ignore separators in quoting with "" and ''. */
        QUAD_SPLIT_UNQUOTE                  = 1 << 5, /* Ignore separators in quoting with "" and '', and remove the quotes. */
        QUAD_SPLIT_DONT_COALESCE_SEPARATORS = 1 << 6, /* Don't treat multiple adjacent separators as one */
        QUAD_SPLIT_RETAIN_ESCAPE            = 1 << 7, /* Treat escape character '\' as any other character without special meaning */
        QUAD_SPLIT_RETAIN_SEPARATORS        = 1 << 8, /* Do not advance the original string pointer past the separator(s) */
} QuadSplitFlags;

#define WHITESPACE        " \t\n\r"

const char *quad_get_unit_dir            (void);
char *      quad_replace_extension       (const char *name,
                                          const char *extension,
                                          const char *extra_prefix,
                                          const char *extra_suffix);
char *      quad_apply_line_continuation (const char *raw_string);
GPtrArray * quad_split_string            (const char *s,
                                          const char *separators,
                                          QuadSplitFlags flags);
char *      quad_escape_words            (GPtrArray *words);
gboolean    quad_fail                    (GError **error,
                                          const char *fmt, ...) G_GNUC_PRINTF (2,3);

void        quad_logv                    (const char *fmt,
                                          va_list     args);
void        quad_log                     (const char *fmt, ...) G_GNUC_PRINTF (1,2);

G_END_DECLS
