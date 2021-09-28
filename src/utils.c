#include "quadlet-config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

#include "utils.h"
#include <stdint.h>

const char **
quad_get_unit_dirs (void)
{
  static const char **unit_dirs = NULL;
  static const char *unit_dirs_default[] = {
    QUADLET_UNIT_DIR_ADMIN,
    QUADLET_UNIT_DIR_DISTRO,
    NULL
  };

  if (unit_dirs == NULL)
    {
      const char *unit_dirs_env = g_getenv ("QUADLET_UNIT_DIRS");
      if (unit_dirs_env != NULL)
        unit_dirs = (const char **)g_strsplit (unit_dirs_env, ":", -1);
      else
        unit_dirs = unit_dirs_default;
    }

  return unit_dirs;
}

char *
quad_replace_extension (const char *name,
                        const char *extension,
                        const char *extra_prefix,
                        const char *extra_suffix)
{
  g_autofree char *base_name = NULL;
  const char *dot;

  if (extension == NULL)
    extension = "";

  if (extra_suffix == NULL)
    extra_suffix = "";

  if (extra_prefix == NULL)
    extra_prefix = "";

  dot = strrchr (name, '.');
  if (dot)
    base_name = g_strndup (name, dot - name);
  else
    base_name = g_strdup (name);

  return g_strconcat (extra_prefix, base_name, extra_suffix, extension, NULL);
}

char *
quad_apply_line_continuation (const char *raw_string)
{
  GString *str = g_string_new ("");
  const char *continuation;

  while (raw_string && *raw_string)
    {
      continuation = strstr (raw_string, "\\\n");
      if (continuation == NULL)
        {
          g_string_append (str, raw_string);
          raw_string = NULL;
        }
      else
        {
          g_string_append_len (str, raw_string, continuation - raw_string);
          g_string_append (str, " ");
          raw_string = continuation + 2;
        }
    }

  return g_string_free (str, FALSE);
}

/* This is based on code from systemd (src/basic/escape.c), marked LGPL-2.1-or-later and is copyrighted by the systemd developers */

int
cunescape_one (const char *p,
               gsize length,
               gunichar *ret,
               gboolean *eight_bit,
               gboolean accept_nul)
{
  int r = 1;

  /* Unescapes C style. Returns the unescaped character in ret.
   * Sets *eight_bit to true if the escaped sequence either fits in
   * one byte in UTF-8 or is a non-unicode literal byte and should
   * instead be copied directly.
   */

  if (length != SIZE_MAX && length < 1)
    return -EINVAL;

  switch (p[0])
    {
    case 'a':
      *ret = '\a';
      break;
    case 'b':
      *ret = '\b';
      break;
    case 'f':
      *ret = '\f';
      break;
    case 'n':
      *ret = '\n';
      break;
    case 'r':
      *ret = '\r';
      break;
    case 't':
      *ret = '\t';
      break;
    case 'v':
      *ret = '\v';
      break;
    case '\\':
      *ret = '\\';
      break;
    case '"':
      *ret = '"';
      break;
    case '\'':
      *ret = '\'';
      break;

    case 's':
      /* This is an extension of the XDG syntax files */
      *ret = ' ';
      break;

    case 'x':
      {
        /* hexadecimal encoding */
        int a, b;

        if (length != SIZE_MAX && length < 3)
          return -EINVAL;

        a = g_ascii_xdigit_value(p[1]);
        if (a < 0)
          return -EINVAL;

        b = g_ascii_xdigit_value(p[2]);
        if (b < 0)
          return -EINVAL;

        /* Don't allow NUL bytes */
        if (a == 0 && b == 0 && !accept_nul)
          return -EINVAL;

        *ret = (a << 4U) | b;
        *eight_bit = TRUE;
        r = 3;
        break;
      }

    case 'u':
      {
        /* C++11 style 16bit unicode */

        int a[4];
        gsize i;
        uint32_t c;

        if (length != SIZE_MAX && length < 5)
          return -EINVAL;

        for (i = 0; i < 4; i++)
          {
            a[i] = g_ascii_xdigit_value(p[1 + i]);
            if (a[i] < 0)
              return a[i];
          }

        c = ((uint32_t) a[0] << 12U) | ((uint32_t) a[1] << 8U) | ((uint32_t) a[2] << 4U) | (uint32_t) a[3];

        /* Don't allow 0 chars */
        if (c == 0 && !accept_nul)
          return -EINVAL;

        *ret = c;
        r = 5;
        break;
      }

  case 'U':
    {
      /* C++11 style 32bit unicode */

      int a[8];
      gsize i;
      gunichar c;

      if (length != SIZE_MAX && length < 9)
        return -EINVAL;

      for (i = 0; i < 8; i++)
        {
          a[i] = g_ascii_xdigit_value(p[1 + i]);
          if (a[i] < 0)
            return a[i];
        }

      c = ((uint32_t) a[0] << 28U) | ((uint32_t) a[1] << 24U) | ((uint32_t) a[2] << 20U) | ((uint32_t) a[3] << 16U) |
        ((uint32_t) a[4] << 12U) | ((uint32_t) a[5] <<  8U) | ((uint32_t) a[6] <<  4U) |  (uint32_t) a[7];

      /* Don't allow 0 chars */
      if (c == 0 && !accept_nul)
        return -EINVAL;

      /* Don't allow invalid code points */
      if (!g_unichar_validate (c))
        return -EINVAL;

      *ret = c;
      r = 9;
      break;
    }

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      {
        /* octal encoding */
        int a, b, c;
        gunichar m;

        if (length != SIZE_MAX && length < 3)
          return -EINVAL;

        a = g_ascii_digit_value(p[0]);
        if (a < 0)
          return -EINVAL;

        b = g_ascii_digit_value(p[1]);
        if (b < 0)
          return -EINVAL;

        c = g_ascii_digit_value(p[2]);
        if (c < 0)
          return -EINVAL;

        /* don't allow NUL bytes */
        if (a == 0 && b == 0 && c == 0 && !accept_nul)
          return -EINVAL;

        /* Don't allow bytes above 255 */
        m = ((uint32_t) a << 6U) | ((uint32_t) b << 3U) | (uint32_t) c;
        if (m > 255)
          return -EINVAL;

        *ret = m;
        *eight_bit = TRUE;
        r = 3;
        break;
      }

    default:
      return -EINVAL;
    }

  return r;
}

/* This is based on code from systemd (src/basic/extract-workd.c), marked LGPL-2.1-or-later and is copyrighted by the systemd developers */

static int
extract_first_word (const char **p, char **ret, const char *separators, QuadSplitFlags flags)
{
  g_autoptr(GString) s = g_string_new ("");
  char quote = 0;                     /* 0 or ' or " */
  gboolean backslash = FALSE;         /* whether we've just seen a backslash */
  char c;
  int r;

  /* Bail early if called after last value or with no input */
  if (!*p)
    goto finish;

  c = **p;

  if (!separators)
    separators = WHITESPACE;

  /* Parses the first word of a string, and returns it in
   * *ret. Removes all quotes in the process. When parsing fails
   * (because of an uneven number of quotes or similar), leaves
   * the pointer *p at the first invalid character. */

  for (;; (*p)++, c = **p)
    {
      if (c == 0)
        goto finish_force_terminate;
      else if (strchr (separators, c))
        {
          if (flags & QUAD_SPLIT_DONT_COALESCE_SEPARATORS)
            {
              if (!(flags & QUAD_SPLIT_RETAIN_SEPARATORS))
                (*p)++;
              goto finish_force_next;
            }
        }
      else
        {
          /* We found a non-blank character, so we will always
           * want to return a string (even if it is empty),
           * allocate it here. */
          break;
        }
    }

  for (;; (*p)++, c = **p)
    {
      if (backslash)
        {
          if (c == 0)
            {
              if ((flags & QUAD_SPLIT_UNESCAPE_RELAX) &&
                  (quote == 0 || flags & QUAD_SPLIT_RELAX))
                {
                  /* If we find an unquoted trailing backslash and we're in
                   * QUAD_SPLIT_UNESCAPE_RELAX mode, keep it verbatim in the
                   * output.
                   *
                   * Unbalanced quotes will only be allowed in QUAD_SPLIT_RELAX
                   * mode, QUAD_SPLIT_UNESCAPE_RELAX mode does not allow them.
                   */
                  g_string_append_c (s, '\\');
                  goto finish_force_terminate;
                }
              if (flags & QUAD_SPLIT_RELAX)
                goto finish_force_terminate;
              return -EINVAL;
            }

          if (flags & (QUAD_SPLIT_CUNESCAPE|QUAD_SPLIT_UNESCAPE_SEPARATORS))
            {
              gboolean eight_bit = FALSE;
              gunichar u;

              if ((flags & QUAD_SPLIT_CUNESCAPE) &&
                  (r = cunescape_one (*p, SIZE_MAX, &u, &eight_bit, FALSE)) >= 0)
                {
                  (*p) += r - 1;
                  g_string_append_unichar (s, u);
                }
              else if ((flags & QUAD_SPLIT_UNESCAPE_SEPARATORS) &&
                       (strchr(separators, **p) || **p == '\\'))
                {
                  /* An escaped separator char or the escape char itself */
                  g_string_append_c (s, c);
                }
              else if (flags & QUAD_SPLIT_UNESCAPE_RELAX)
                {
                  g_string_append_c (s, '\\');
                  g_string_append_c (s, c);
                }
              else
                return -EINVAL;
            }
          else
            g_string_append_c (s, c);

          backslash = FALSE;
        }
      else if (quote != 0)
        {
          /* inside either single or double quotes */
          for (;; (*p)++, c = **p)
            {
              if (c == 0)
                {
                  if (flags & QUAD_SPLIT_RELAX)
                    goto finish_force_terminate;
                  return -EINVAL;
                }
              else if (c == quote)
                {        /* found the end quote */
                  quote = 0;
                  if (flags & QUAD_SPLIT_UNQUOTE)
                    break;
                }
              else if (c == '\\' && !(flags & QUAD_SPLIT_RETAIN_ESCAPE))
                {
                  backslash = TRUE;
                  break;
                }

              g_string_append_c (s, c);

              if (quote == 0)
                break;
            }
        }
      else
        {
          for (;; (*p)++, c = **p)
            {
              if (c == 0)
                {
                  goto finish_force_terminate;
                }
              else if ( (c == '\'' || c == '"') && (flags & (QUAD_SPLIT_KEEP_QUOTE | QUAD_SPLIT_UNQUOTE)))
                {
                  quote = c;
                  if (flags & QUAD_SPLIT_UNQUOTE)
                    break;
                }
              else if (c == '\\' && !(flags & QUAD_SPLIT_RETAIN_ESCAPE))
                {
                  backslash = TRUE;
                  break;
                }
              else if (strchr (separators, c))
                {
                  if (flags & QUAD_SPLIT_DONT_COALESCE_SEPARATORS)
                    {
                      if (!(flags & QUAD_SPLIT_RETAIN_SEPARATORS))
                        (*p)++;
                      goto finish_force_next;
                    }

                  if (!(flags & QUAD_SPLIT_RETAIN_SEPARATORS))
                    /* Skip additional coalesced separators. */
                    for (;; (*p)++, c = **p)
                      {
                        if (c == 0)
                          goto finish_force_terminate;
                        if (!strchr (separators, c))
                          break;
                      }
                  goto finish;
                }

              g_string_append_c (s, c);

              if (quote != 0)
                break;
            }
        }
    }

 finish_force_terminate:
  *p = NULL;

 finish:
  if (s->len == 0)
    {
      *p = NULL;
      *ret = NULL;
      return 0;
    }

 finish_force_next:
  *ret = g_string_free (g_steal_pointer (&s), FALSE);

  return 1;
}

GPtrArray *
quad_split_string (const char *s, const char *separators, QuadSplitFlags flags)
{
  g_autoptr(GPtrArray) l = g_ptr_array_new_with_free_func (g_free);
  int r;

  for (;;)
    {
      g_autofree char *word = NULL;

      r = extract_first_word (&s, &word, separators, flags);
      if (r < 0)
        return NULL;

      if (r == 0)
        break;

      g_ptr_array_add (l, g_steal_pointer (&word));
    }

  return g_steal_pointer (&l);
}

static gboolean
char_need_escape (gunichar c)
{
  if (c > 128)
    return FALSE; /* unicode is ok */

  return
    g_ascii_iscntrl (c) ||
    g_ascii_isspace (c) ||
    c == '"' ||
    c == '\'' ||
    c == '\\' ||
    c == ';';
}


static gboolean
word_need_escape (const char *word)
{
  for (; *word != 0; word = g_utf8_find_next_char (word, NULL))
    {
      gunichar c = g_utf8_get_char (word);
      if (char_need_escape (c))
        return TRUE;
    }

  return FALSE;
}

static void
append_escape_word (GString *escaped,
                    const char *word)
{
  g_string_append_c (escaped, '"');
  while (*word != 0)
    {
      const char *next = g_utf8_find_next_char (word, NULL);
      gunichar c = g_utf8_get_char (word);

      if (char_need_escape (c))
        {
          switch (c)
            {
            case '\n':
              g_string_append (escaped, "\\n");
              break;
            case '\r':
              g_string_append (escaped, "\\r");
              break;
            case '\t':
              g_string_append (escaped, "\t");
              break;
            case '\f':
              g_string_append (escaped, "\\f");
              break;
            case '\\':
              g_string_append (escaped, "\\\\");
              break;
            case ';':
              g_string_append (escaped, "\\;");
              break;
            case ' ':
              g_string_append (escaped, " ");
              break;
            case '"':
              g_string_append (escaped, "\"");
              break;
            case '\'':
              g_string_append (escaped, "'");
              break;
            default:
              g_string_append_printf (escaped, "\\x%.2x", c);
              break;
            }
        }
      else
        {
          g_string_append_len (escaped, word, next - word);
        }

      word = next;
    }

  g_string_append_c (escaped, '"');
}

char *
quad_escape_words (GPtrArray *words)
{
  g_autoptr(GString) escaped = g_string_new ("");

  for (guint i = 0 ; i < words->len; i++)
    {
      const char *word = g_ptr_array_index (words, i);
      if (i != 0)
        g_string_append (escaped, " ");
      if (word_need_escape (word))
        append_escape_word (escaped, word);
      else
        g_string_append (escaped, word);
    }
  return g_string_free (g_steal_pointer (&escaped), FALSE);
}

gboolean
quad_fail (GError    **error,
           const char *fmt,
           ...)
{
  if (error == NULL)
    return FALSE;

  va_list args;
  va_start (args, fmt);
  GError *new = g_error_new_valist (G_FILE_ERROR, G_FILE_ERROR_FAILED, fmt, args);
  va_end (args);
  g_propagate_error (error, g_steal_pointer (&new));
  return FALSE;
}

static gboolean
log_to_kmsg (const char *line)
{
  static int dev_kmsg_fd = -2;
  int res;

  if (dev_kmsg_fd == -2)
    dev_kmsg_fd = open ("/dev/kmsg", O_WRONLY);

  if (dev_kmsg_fd < 0)
    return FALSE; /* Failed open */

  res = write (dev_kmsg_fd, line, strlen (line));
  if (res < 0)
    return FALSE;

   return TRUE;
}

void
quad_logv (const char *fmt,
           va_list     args)
{
  g_autofree char *s = g_strdup_vprintf (fmt, args);
  g_autofree char *log = g_strdup_printf ("quadlet-generator[%d]: %s\n", getpid (), s);

  if (!log_to_kmsg (log))
    {
      /* If we can't log, print to stderr */
      fputs (s, stderr);
      fputs ("\n", stderr);
      fflush (stderr);
    }
}

void
quad_log (const char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  quad_logv (fmt, args);
  va_end (args);
}

static gboolean do_debug = FALSE;

void
quad_enable_debug (void)
{
  do_debug = TRUE;
}

void
quad_debug (const char *fmt, ...)
{
  va_list args;
  if (do_debug)
    {
      va_start (args, fmt);
      quad_logv (fmt, args);
      va_end (args);
    }
}

uid_t
quad_lookup_host_uid (const char *user,
                      GError    **error)
{
  char *endp;
  long long res;
  struct passwd *pw;

  /* First special case numeric ids */

  res = strtoll (user, &endp, 10);

  if (endp != user && *endp == 0)
    {
      /* On linux, uids are uint32 values, that can't be (uint32)-1 */
      if (res < 0 || res > UINT32_MAX || res == (uid_t)-1)
        {
          quad_fail (error, "Invalid numerical uid '%s'", user);
          return (uid_t)-1;
        }

      return res;
    }

  pw = getpwnam (user);
  if (pw == NULL)
    {
      quad_fail (error, "Unknown user '%s'", user);
      return (uid_t)-1;
    }

  return pw->pw_uid;
}


gid_t
quad_lookup_host_gid (const char *group,
                      GError    **error)
{
  char *endp;
  long long res;
  struct group *gp;

  /* First special case numeric ids */

  res = strtoll (group, &endp, 10);

  if (endp != group && *endp == 0)
    {
      /* On linux, gids are uint32 values, that can't be (uint32)-1 */
      if (res < 0 || res > UINT32_MAX || res == (gid_t)-1)
        {
          quad_fail (error, "Invalid numerical gid '%s'", group);
          return (uid_t)-1;
        }

      return res;
    }

  gp = getgrnam (group);
  if (gp == NULL)
    {
      quad_fail (error, "Unknown group '%s'", group);
      return (uid_t)-1;
    }

  return gp->gr_gid;
}
