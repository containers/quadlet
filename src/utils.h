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

const char **         quad_get_unit_dirs           (void);
char *                quad_replace_extension       (const char     *name,
                                                    const char     *extension,
                                                    const char     *extra_prefix,
                                                    const char     *extra_suffix);
char *                quad_apply_line_continuation (const char     *raw_string);
GPtrArray *           quad_split_string            (const char     *s,
                                                    const char     *separators,
                                                    QuadSplitFlags  flags);
char *                quad_escape_words            (GPtrArray      *words);

gboolean              quad_fail                    (GError **error,
                                                    const char *fmt, ...) G_GNUC_PRINTF (2, 3);
void                  quad_logv                    (const char     *fmt,
                                                    va_list         args);
void                  quad_log                     (const char *fmt, ...) G_GNUC_PRINTF (1,2);
void                  quad_enable_debug            (void);
void                  quad_debug                   (const char *fmt, ...) G_GNUC_PRINTF (1,2);

#define _QUAD_CONCAT(a, b)  a##b
#define _QUAD_CONCAT_INDIRECT(a, b) _QUAD_CONCAT(a, b)
#define _QUAD_MAKE_ANONYMOUS(a) _QUAD_CONCAT_INDIRECT(a, __COUNTER__)

#define _QUAD_HASH_TABLE_FOREACH_IMPL_KV(guard, ht, it, kt, k, vt, v)          \
    gboolean guard = TRUE;                                                     \
    G_STATIC_ASSERT (sizeof (kt) == sizeof (void*));                           \
    G_STATIC_ASSERT (sizeof (vt) == sizeof (void*));                           \
    for (GHashTableIter it;                                                    \
         guard && ({ g_hash_table_iter_init (&it, ht), TRUE; });               \
         guard = FALSE)                                                        \
            for (kt k; guard; guard = FALSE)                                   \
                for (vt v; g_hash_table_iter_next (&it, (gpointer)&k, (gpointer)&v);)


/* Cleaner method to iterate over a GHashTable. I.e. rather than
 *
 *   gpointer k, v;
 *   GHashTableIter it;
 *   g_hash_table_iter_init (&it, table);
 *   while (g_hash_table_iter_next (&it, &k, &v))
 *     {
 *       const char *str = k;
 *       GPtrArray *arr = v;
 *       ...
 *     }
 *
 * you can simply do
 *
 *   QUAD_HASH_TABLE_FOREACH_IT (table, it, const char*, str, GPtrArray*, arr)
 *     {
 *       ...
 *     }
 *
 * All variables are scoped within the loop. You may use the `it` variable as
 * usual, e.g. to remove an element using g_hash_table_iter_remove(&it). There
 * are shorter variants for the more common cases where you do not need access
 * to the iterator or to keys/values:
 *
 *   QUAD_HASH_TABLE_FOREACH (table, const char*, str) { ... }
 *   QUAD_HASH_TABLE_FOREACH_V (table, MyData*, data) { ... }
 *   QUAD_HASH_TABLE_FOREACH_KV (table, const char*, str, MyData*, data) { ... }
 *
 */
#define QUAD_HASH_TABLE_FOREACH_IT(ht, it, kt, k, vt, v) \
    _QUAD_HASH_TABLE_FOREACH_IMPL_KV( \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_guard_), ht, it, kt, k, vt, v)

/* Variant of QUAD_HASH_TABLE_FOREACH without having to specify an iterator. An
 * anonymous iterator will be created. */
#define QUAD_HASH_TABLE_FOREACH_KV(ht, kt, k, vt, v) \
    _QUAD_HASH_TABLE_FOREACH_IMPL_KV( \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_guard_), ht, \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_it_), kt, k, vt, v)

/* Variant of QUAD_HASH_TABLE_FOREACH_KV which omits unpacking keys. */
#define QUAD_HASH_TABLE_FOREACH_V(ht, vt, v) \
    _QUAD_HASH_TABLE_FOREACH_IMPL_KV( \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_guard_), ht, \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_it_), \
         gpointer, _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_v_), \
         vt, v)

/* Variant of QUAD_HASH_TABLE_FOREACH_KV which omits unpacking vals. */
#define QUAD_HASH_TABLE_FOREACH(ht, kt, k) \
    _QUAD_HASH_TABLE_FOREACH_IMPL_KV( \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_guard_), ht, \
         _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_it_), kt, k, \
         gpointer, _QUAD_MAKE_ANONYMOUS(_glnx_ht_iter_v_))


G_END_DECLS
