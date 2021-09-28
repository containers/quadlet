#include "quadlet-config.h"

#include "unitfile.h"
#include "utils.h"

typedef struct
{
  char *key;  /* NULL for comments */
  char *value;
} QuadUnitLine;

typedef struct {
  char *name;
  GPtrArray *comments; /* Comments before the groupname */
  GPtrArray *lines;

} QuadUnitGroup;

struct _QuadUnitFile
{
  GObject parent_instance;

  GPtrArray *groups;
  GHashTable *group_hash; /* keys/values owned by groups array */

  char *path;

  /* During parsing: */
  QuadUnitGroup *current_group;
  GPtrArray *pending_comments;
  int line_nr;
};

G_DEFINE_TYPE (QuadUnitFile, quad_unit_file, G_TYPE_OBJECT)

static QuadUnitGroup *quad_unit_file_ensure_group (QuadUnitFile *self,
                                                   const char *group_name);

static QuadUnitLine *
quad_unit_line_new (const char *key, const char *value, gsize value_len)
{
  QuadUnitLine *line = g_new0 (QuadUnitLine, 1);

  line->key = g_strdup (key);
  line->value = g_strndup (value, value_len);

  return line;
}

static QuadUnitLine *
quad_unit_line_copy (QuadUnitLine *line)
{
  return quad_unit_line_new (line->key, line->value, strlen (line->value));
}

static void
quad_unit_line_set (QuadUnitLine *line, const char *value)
{
  g_free (line->value);
  line->value = g_strdup (value);
}

static gboolean
quad_unit_line_is (QuadUnitLine *line, const char *key)
{
  return line->key != NULL && strcmp (key, line->key) == 0;
}

static gboolean
quad_unit_line_is_empty (QuadUnitLine *line)
{
  return line->value[0] == 0;
}

static void
quad_unit_line_free (QuadUnitLine *line)
{
  g_free (line->key);
  g_free (line->value);
  g_free (line);
}

static QuadUnitGroup *
quad_unit_group_new (const char *name)
{
  QuadUnitGroup *group = g_new0 (QuadUnitGroup, 1);
  group->name = g_strdup (name);
  group->comments = g_ptr_array_new_with_free_func ((GDestroyNotify)quad_unit_line_free);
  group->lines = g_ptr_array_new_with_free_func ((GDestroyNotify)quad_unit_line_free);
  return group;
}

static void
quad_unit_group_add (QuadUnitGroup *group,
                     const char *key,
                     const char *value)
{
  g_ptr_array_add (group->lines,
                   quad_unit_line_new (key, value, strlen (value)));
}


static QuadUnitLine *
quad_unit_group_find_last (QuadUnitGroup *group,
                           const char *key,
                           guint *index_out)
{
  for (int i = group->lines->len - 1; i >= 0; i--)
    {
      QuadUnitLine *line = g_ptr_array_index (group->lines, i);
      if (quad_unit_line_is (line, key))
        {
          if (index_out)
            *index_out = i;
          return line;
        }
    }

  return NULL;
}

static void
quad_unit_group_merge (QuadUnitGroup *group,
                       QuadUnitGroup *source)
{
  for (guint i = 0; i < source->comments->len; i++)
    {
      QuadUnitLine *line = g_ptr_array_index (source->comments, i);
      g_ptr_array_add (group->comments, quad_unit_line_copy (line));
    }

  for (guint i = 0; i < source->lines->len; i++)
    {
      QuadUnitLine *line = g_ptr_array_index (source->lines, i);
      g_ptr_array_add (group->lines, quad_unit_line_copy (line));
    }
}

static void
quad_unit_group_free (QuadUnitGroup *group)
{
  g_ptr_array_free (group->comments, TRUE);
  g_ptr_array_free (group->lines, TRUE);
  g_free (group->name);
  g_free (group);
}

QuadUnitFile *
quad_unit_file_new_from_path (const char *path, GError **error)
{
  g_autofree char *data = NULL;
  g_autoptr(QuadUnitFile) unit = NULL;

  if (!g_file_get_contents (path, &data, NULL, error))
    {
      g_prefix_error (error, "Failed to open %s: ", path);
      return NULL;
    }

  unit = quad_unit_file_new ();

  if (!quad_unit_file_parse (unit, data, error))
    return NULL;

  unit->path = g_strdup (path);

  return g_steal_pointer(&unit);
}

const char *
quad_unit_file_get_path (QuadUnitFile  *self)
{
  return self->path;
}

void
quad_unit_file_set_path (QuadUnitFile  *self,
                         const char *path)
{
  g_free (self->path);
  self->path = g_strdup (path);
}

QuadUnitFile *
quad_unit_file_new (void)
{
  g_autoptr(QuadUnitFile) unit = g_object_new (QUAD_TYPE_UNIT_FILE, NULL);

  return g_steal_pointer (&unit);
}

void
quad_unit_file_merge (QuadUnitFile *self,
                      QuadUnitFile *source)
{
  for (guint i = 0; i < source->groups->len; i++)
    {
      QuadUnitGroup *src_group = g_ptr_array_index (source->groups, i);
      QuadUnitGroup *group =  quad_unit_file_ensure_group (self, src_group->name);
      quad_unit_group_merge (group, src_group);
    }
}

QuadUnitFile *
quad_unit_file_copy (QuadUnitFile *self)
{
  QuadUnitFile *copy = quad_unit_file_new ();
  quad_unit_file_merge (copy, self);
  return copy;
}

static gboolean
line_is_comment (const char *line, const char *line_end)
{
  return line == line_end || *line == '#' || *line == ';';
}

static gboolean
line_is_group (const char *line, const char *line_end)
{
  char *p;

  if (line == line_end)
    return FALSE;

  p = (char *) line;
  if (*p != '[')
    return FALSE;

  p++;

  while (p < line_end && *p != ']')
    p = g_utf8_find_next_char (p, NULL);

  if (p >= line_end || *p != ']')
    return FALSE;

  /* silently accept whitespace after the ] */
  p = g_utf8_find_next_char (p, NULL);
  while (p < line_end && (*p == ' ' || *p == '\t'))
    p = g_utf8_find_next_char (p, NULL);

  if (p != line_end)
    return FALSE;

  return TRUE;
}

static gboolean
line_is_key_value_pair (const char *line,
                        const char *line_end)
{
  char *p;

  if (line == line_end)
    return FALSE;

  p = (char *) g_utf8_strchr (line, line_end - line, '=');
  if (!p)
    return FALSE;

  /* Key must be non-empty
   */
  if (*p == line[0])
    return FALSE;

  return TRUE;
}

static gboolean
is_valid_group_name (const gchar *name)
{
  char *p, *q;

  if (name == NULL)
    return FALSE;

  p = q = (char *) name;
  while (*q && *q != ']' && *q != '[' && !g_ascii_iscntrl (*q))
    q = g_utf8_find_next_char (q, NULL);

  if (*q != '\0' || q == p)
    return FALSE;

  return TRUE;
}

static gboolean
is_valid_key_name (const char *name)
{
  char *p, *q;

  if (name == NULL)
    return FALSE;

  p = q = (char *) name;

  /* We accept a little more than the desktop entry spec says,
   * since gnome-vfs uses mime-types as keys in its cache.
   */
  while (*q && *q != '=' && *q != '[' && *q != ']')
    q = g_utf8_find_next_char (q, NULL);

  /* No empty keys, please */
  if (q == p)
    return FALSE;

  /* We accept spaces in the middle of keys to not break
   * existing apps, but we don't tolerate initial or final
   * spaces, which would lead to silent corruption when
   * rereading the file.
   */
  if (*p == ' ' || q[-1] == ' ')
    return FALSE;

  if (*q == '[')
    {
      q++;
      while (*q && (g_unichar_isalnum (g_utf8_get_char_validated (q, -1)) || *q == '-' || *q == '_' || *q == '.' || *q == '@'))
        q = g_utf8_find_next_char (q, NULL);

      if (*q != ']')
        return FALSE;

      q++;
    }

  if (*q != '\0')
    return FALSE;

  return TRUE;
}


static QuadUnitGroup *
quad_unit_file_lookup_group (QuadUnitFile *self,
                             const char *group_name)
{
  return g_hash_table_lookup (self->group_hash, group_name);
}

static QuadUnitGroup *
quad_unit_file_ensure_group (QuadUnitFile *self,
                             const char *group_name)
{
  QuadUnitGroup *group = quad_unit_file_lookup_group (self, group_name);

  if (group == NULL)
    {
      group = quad_unit_group_new (group_name);
      g_ptr_array_add (self->groups, group);
      g_hash_table_insert (self->group_hash, group->name, group);
    }

  return group;
}

static gboolean
quad_unit_file_parse_comment (QuadUnitFile *self,
                              const char *line,
                              const char *line_end,
                              G_GNUC_UNUSED GError **error)
{
  QuadUnitLine *l = quad_unit_line_new (NULL, line, line_end - line);

  g_ptr_array_add (self->pending_comments, l);

  return TRUE;
}

static void
quad_unit_file_flush_pending_comments (QuadUnitFile *self,
                                       GPtrArray *to)
{
  gsize n_pending = self->pending_comments->len;

  if (n_pending > 0)
    {
      g_autofree QuadUnitLine **pending = (QuadUnitLine **)g_ptr_array_free (self->pending_comments, FALSE);
      self->pending_comments = g_ptr_array_new_with_free_func ((GDestroyNotify)quad_unit_line_free);

      for (gsize i = 0; i < n_pending; i++)
        g_ptr_array_add (to, pending[i]);
    }
}

static gboolean
quad_unit_file_parse_group (QuadUnitFile *self,
                            const char *line,
                            const char *line_end,
                            GError **error)
{
  g_autofree char *group_name = NULL;
  const char *group_name_start, *group_name_end;

  /* advance past opening '['  */
  group_name_start = line + 1;
  group_name_end = line_end - 1;

  while (*group_name_end != ']')
    group_name_end--;

  group_name = g_strndup (group_name_start,
                          group_name_end - group_name_start);

  if (!is_valid_group_name (group_name))
    {
      g_set_error (error, G_KEY_FILE_ERROR,
                   G_KEY_FILE_ERROR_PARSE,
                   "Invalid group name: %s", group_name);
      return FALSE;
    }

  self->current_group = quad_unit_file_ensure_group (self, group_name);

  if (self->pending_comments->len > 0)
    {
      QuadUnitLine *first_comment = g_ptr_array_index (self->pending_comments, 0);

      /* Remove one newline between groups, which is re-added on printing, see quad_unit_group_print()*/
      if (quad_unit_line_is_empty (first_comment))
        g_ptr_array_remove_index (self->pending_comments, 0);

      quad_unit_file_flush_pending_comments (self, self->current_group->comments);
    }

  return TRUE;
}

static gboolean
quad_unit_file_parse_key_value_pair (QuadUnitFile *self,
                                     const char *line,
                                     const char *line_end,
                                     GError **error)
{
  g_autofree char *key = NULL;
  char *key_end, *value_start;

  if (self->current_group == NULL)
    {
      g_set_error_literal (error, G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_GROUP_NOT_FOUND,
                           "Key file does not start with a group");
      return FALSE;
    }

  key_end = value_start = strchr (line, '=');
  g_assert (key_end != NULL);

  key_end--;
  value_start++;

  /* Pull the key name from the line (chomping trailing whitespace) */
  while (g_ascii_isspace (*key_end))
    key_end--;

  key = g_strndup (line, key_end + 1 - line);
  if (!is_valid_key_name (key))
    {
      g_set_error (error, G_KEY_FILE_ERROR,
                   G_KEY_FILE_ERROR_PARSE,
                   "Invalid key name: %s", key);
      return FALSE;
    }

  /* Pull the value from the line (chugging leading whitespace) */
  while (value_start < line_end && g_ascii_isspace (*value_start))
    value_start++;

  quad_unit_file_flush_pending_comments (self, self->current_group->lines);
  g_ptr_array_add (self->current_group->lines,
                   quad_unit_line_new (key, value_start, line_end - value_start));

  return TRUE;
}

static gboolean
quad_unit_file_parse_line (QuadUnitFile *self,
                           const char *line,
                           const char *line_end,
                           GError **error)
{
  if (line_is_comment (line, line_end))
    return quad_unit_file_parse_comment (self, line, line_end, error);
  else if (line_is_group (line, line_end))
    return quad_unit_file_parse_group (self, line, line_end, error);
  else if (line_is_key_value_pair (line, line_end))
    return quad_unit_file_parse_key_value_pair (self, line,
                                                line_end,
                                                error);
  else
    {
      g_autofree char *line_utf8 = g_utf8_make_valid (line, line_end - line);
      g_set_error (error, G_KEY_FILE_ERROR,
                   G_KEY_FILE_ERROR_PARSE,
                   "File contains line %d: “%s” which is not a key-value pair, group, or comment",
                   self->line_nr,
                   line_utf8);
      return FALSE;
    }
}

gboolean
quad_unit_file_parse (QuadUnitFile *self,
                      const char *data,
                      GError **error)
{
  const char *line;
  const char *data_end = data + strlen (data);

  line = data;
  while (line < data_end)
    {
      const char *next;
      const char *endofline = strchr(line, '\n');
      int n_lines = 1;

      if (endofline == NULL)
        {
          endofline = data_end;
          next = data_end;
        }
      else
        {
          next = endofline + 1;
        }

      /* Handle multi-line continuations */
      /* Note: This doesn't support coments in the middle of the continuation, which systemd does */
      if (line_is_key_value_pair (line, endofline))
        {
          while (endofline < data_end && endofline[-1] == '\\')
            {
              const char *next_endofline = strchr(next, '\n');

              if (next_endofline == NULL)
                {
                  endofline = data_end;
                  next = data_end;
                }
              else
                {
                  endofline = next_endofline;
                  next = next_endofline + 1;
                }
            }
        }

      if (!quad_unit_file_parse_line (self, line, endofline, error))
        return FALSE;

      self->line_nr += n_lines;
      line = next;
    }

  /* This drops comments in files without groups, but YOLO */
  if (self->current_group)
    quad_unit_file_flush_pending_comments (self, self->current_group->lines);

  return TRUE;
}

static void
quad_unit_line_print (QuadUnitLine *line, GString *str)
{
  if (line->key == NULL)
    g_string_append_printf (str, "%s\n", line->value);
  else
    g_string_append_printf (str, "%s=%s\n", line->key, line->value);
}

static void
quad_unit_group_print (QuadUnitGroup *group, GString *str)
{
  guint i;

  for (i = 0; i < group->comments->len; i++)
    quad_unit_line_print (g_ptr_array_index (group->comments, i), str);
  g_string_append_printf(str, "[%s]\n", group->name);
  for (i = 0; i < group->lines->len; i++)
    quad_unit_line_print (g_ptr_array_index (group->lines, i), str);
}

void
quad_unit_file_print (QuadUnitFile *self, GString *str)
{
  guint i;

  for (i = 0; i < self->groups->len; i++)
    {
      /* We always add a newline between groups, and strip one if it exists during
         parsing. This looks nicer, and avoids issues of duplicate newlines when
         merging groups or missing ones when creating new groups */
      if (i != 0)
        g_string_append_printf (str, "\n");

      quad_unit_group_print (g_ptr_array_index (self->groups, i), str);
    }
}

const char *
quad_unit_file_lookup_last_raw (QuadUnitFile *self,
                                const char *group_name,
                                const char *key)
{
  QuadUnitGroup *group;
  QuadUnitLine *line;

  group = quad_unit_file_lookup_group (self, group_name);
  if (group == NULL)
    return NULL;

  line = quad_unit_group_find_last (group, key, NULL);
  if (line)
    return line->value;

  return NULL;
}

char *
quad_unit_file_lookup_last (QuadUnitFile  *self,
                            const char    *group_name,
                            const char    *key)
{
  const char *raw = quad_unit_file_lookup_last_raw (self, group_name, key);
  if (raw == NULL)
    return NULL;

  return quad_apply_line_continuation (raw);
}

char *
quad_unit_file_lookup (QuadUnitFile  *self,
                       const char    *group_name,
                       const char    *key)
{
  char *val = quad_unit_file_lookup_last (self, group_name, key);
  if (val == NULL)
    return NULL;

  return g_strchomp (val);
}

gboolean
quad_unit_file_lookup_boolean (QuadUnitFile  *self,
                               const char    *group_name,
                               const char    *key,
                               gboolean       default_value)
{
  g_autofree char *val = quad_unit_file_lookup (self, group_name, key);
  if (val == NULL || *val == 0)
    return default_value;

  return
    g_ascii_strcasecmp (val, "1") == 0 ||
    g_ascii_strcasecmp (val, "yes") == 0 ||
    g_ascii_strcasecmp (val, "true") == 0 ||
    g_ascii_strcasecmp (val, "on") == 0;
}

long
quad_unit_file_lookup_int (QuadUnitFile  *self,
                           const char    *group_name,
                           const char    *key,
                           long           default_value)
{
  long res;
  char *endp;
  g_autofree char *val = quad_unit_file_lookup (self, group_name, key);
  if (val == NULL || *val == 0)
    return default_value;

  /* Convert first part, if any to int */
  res = strtol (val, &endp, 10);
  if (endp != key)
    return res;

  /* Otherwise return default value */
  return default_value;
}

uid_t
quad_unit_file_lookup_uid (QuadUnitFile  *self,
                           const char    *group_name,
                           const char    *key,
                           uid_t          default_value,
                           GError       **error)
{
  g_autofree char *val = quad_unit_file_lookup (self, group_name, key);
  if (val == NULL || *val == 0)
    {
      if (default_value == (uid_t)-1)
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "No key %s", key);
      return default_value;
    }

  return quad_lookup_host_uid (val, error);
}

gid_t
quad_unit_file_lookup_gid (QuadUnitFile  *self,
                           const char    *group_name,
                           const char    *key,
                           gid_t          default_value,
                           GError       **error)
{
  g_autofree char *val = quad_unit_file_lookup (self, group_name, key);
  if (val == NULL || *val == 0)
    {
      if (default_value == (gid_t)-1)
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "No key %s", key);
      return default_value;
    }

  return quad_lookup_host_gid (val, error);
}


const char **
quad_unit_file_lookup_all_raw (QuadUnitFile *self,
                               const char *group_name,
                               const char *key)
{
  QuadUnitGroup *group;
  g_autoptr(GPtrArray) res = g_ptr_array_new ();

  group = quad_unit_file_lookup_group (self, group_name);
  if (group != NULL)
    {
      for (guint i = 0; i < group->lines->len; i++)
        {
          QuadUnitLine *line = g_ptr_array_index (group->lines, i);
          if (quad_unit_line_is (line, key))
            {
              if (*line->value == 0)
                {
                  /* Empty value clears all before */
                  g_ptr_array_set_size (res, 0);
                }
              else
                {
                  g_ptr_array_add (res, line->value);
                }
            }
        }
    }

  g_ptr_array_add (res, NULL);
  return (const char **)g_ptr_array_free (g_steal_pointer (&res), FALSE);
}

char **
quad_unit_file_lookup_all (QuadUnitFile *self,
                           const char *group_name,
                           const char *key)
{
  char **res = (char **)quad_unit_file_lookup_all_raw (self, group_name, key);
  if (res != NULL)
    {
      for (int i = 0; res[i] != NULL; i++)
        {
          res[i] = quad_apply_line_continuation (res[i]);
        }
    }

  return res;
}

gboolean
quad_unit_file_has_group (QuadUnitFile  *self,
                          const char    *group_name)
{
  QuadUnitGroup *group;

  group = quad_unit_file_lookup_group (self, group_name);
  return group != NULL;
}

const char **
quad_unit_file_list_groups (QuadUnitFile  *self)
{
  g_autoptr(GPtrArray) res = g_ptr_array_new ();

  for (guint i = 0; i < self->groups->len; i++)
    {
      QuadUnitGroup *group = g_ptr_array_index (self->groups, i);
      g_ptr_array_add (res, group->name);
    }

  g_ptr_array_add (res, NULL);
  return (const char **)g_ptr_array_free (g_steal_pointer (&res), FALSE);
}

const char **
quad_unit_file_list_keys (QuadUnitFile  *self,
                          const char    *group_name)
{
  QuadUnitGroup *group;
  g_autoptr(GHashTable) res = g_hash_table_new (g_str_hash, g_str_equal);

  group = quad_unit_file_lookup_group (self, group_name);
  if (group != NULL)
    {
      for (guint i = 0; i < group->lines->len; i++)
        {
          QuadUnitLine *line = g_ptr_array_index (group->lines, i);
          if (line->key != NULL)
            g_hash_table_add (res, line->key);
        }
    }

  return (const char **)g_hash_table_get_keys_as_array (res, NULL);
}

void
quad_unit_file_set (QuadUnitFile  *self,
                    const char    *group_name,
                    const char    *key,
                    const char    *value)
{
  QuadUnitGroup *group;
  QuadUnitLine *line;

  group = quad_unit_file_ensure_group (self, group_name);

  line = quad_unit_group_find_last (group, key, NULL);
  if (line)
    quad_unit_line_set (line, value);
  else
    quad_unit_group_add (group, key, value);
}

void
quad_unit_file_setv (QuadUnitFile  *self,
                     const char    *group_name,
                     ...)
{
  va_list args;
  const char *key;
  const char *value;

  va_start (args, group_name);

  while ((key = va_arg(args, char *)) != NULL)
    {
      value = va_arg(args, char *);
      quad_unit_file_set (self, group_name, key, value);
    }

  va_end (args);
}


void
quad_unit_file_add (QuadUnitFile  *self,
                    const char    *group_name,
                    const char    *key,
                    const char    *value)
{
  QuadUnitGroup *group;

  group = quad_unit_file_ensure_group (self, group_name);
  quad_unit_group_add (group, key, value);
}

void
quad_unit_file_unset (QuadUnitFile  *self,
                      const char    *group_name,
                      const char    *key)
{
  QuadUnitGroup *group = quad_unit_file_lookup_group (self, group_name);

  if (group == NULL)
    return;

  /* We iterate backwards to avoid the removal affecting the iteration */
  for (int i = group->lines->len - 1; i >= 0; i--)
    {
      QuadUnitLine *line = g_ptr_array_index (group->lines, i);
      if (quad_unit_line_is (line, key))
        g_ptr_array_remove_index (group->lines, i);
    }
}

void
quad_unit_file_remove_group (QuadUnitFile  *self,
                             const char    *group_name)
{
  QuadUnitGroup *group = quad_unit_file_lookup_group (self, group_name);

  if (group)
    {
      g_hash_table_remove (self->group_hash, group->name);
      g_ptr_array_remove (self->groups, group);
    }
}

void
quad_unit_file_rename_group (QuadUnitFile  *self,
                             const char    *group_name,
                             const char    *new_name)
{
  QuadUnitGroup *group = quad_unit_file_lookup_group (self, group_name);
  QuadUnitGroup *new_group = quad_unit_file_lookup_group (self, new_name);

  if (group == NULL || group == new_group)
    return;

  if (new_group == NULL)
    {
      /* New group doesn't exist, just rename in-place */
      g_hash_table_remove (self->group_hash, group->name);
      g_free (group->name);
      group->name = g_strdup (new_name);
      g_hash_table_insert (self->group_hash, group->name, group);
    }
  else
    {
      /* merge to existing group and delete old */
      new_group = quad_unit_file_ensure_group (self, new_name);

      quad_unit_group_merge (new_group, group);

      g_hash_table_remove (self->group_hash, group->name);
      g_ptr_array_remove (self->groups, group);
    }
}

static void
quad_unit_file_finalize (GObject *object)
{
  QuadUnitFile *self = (QuadUnitFile *)object;

  g_ptr_array_free (self->groups, TRUE);
  g_hash_table_destroy (self->group_hash);
  g_ptr_array_free (self->pending_comments, TRUE);
  g_free (self->path);

  G_OBJECT_CLASS (quad_unit_file_parent_class)->finalize (object);
}

static void
quad_unit_file_class_init (QuadUnitFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = quad_unit_file_finalize;
}

static void
quad_unit_file_init (QuadUnitFile *self)
{
  self->groups = g_ptr_array_new_with_free_func ((GDestroyNotify)quad_unit_group_free);
  self->group_hash = g_hash_table_new (g_str_hash, g_str_equal);

  self->line_nr = 1;
  self->pending_comments = g_ptr_array_new_with_free_func ((GDestroyNotify)quad_unit_line_free);
}
