#include "quadlet-config.h"

#include "podman.h"
#include "utils.h"

struct QuadPodman {
  GPtrArray *args;
};

QuadPodman *quad_podman_new (const char *command, const char *sub_command)
{
  QuadPodman *podman = g_new0 (QuadPodman, 1);

  podman->args = g_ptr_array_new_with_free_func (g_free);
  quad_podman_add (podman, "/usr/bin/podman");

  if (command)
    quad_podman_add (podman, command);

  if (sub_command)
    quad_podman_add (podman, sub_command);

  return podman;
}

void
quad_podman_free (QuadPodman *podman)
{
  g_ptr_array_free (podman->args, TRUE);
  g_free (podman);
}

void
quad_podman_add (QuadPodman *podman,
                 const char *arg)
{
  g_ptr_array_add (podman->args, g_strdup (arg));
}

void
quad_podman_addf (QuadPodman *podman,
                  const char *fmt,
                  ...)
{
  va_list args;
  va_start (args, fmt);
  g_ptr_array_add (podman->args,
                   g_strdup_vprintf (fmt, args));
  va_end (args);
}

void
quad_podman_addv (QuadPodman *podman,
                  ...)
{
  va_list args;
  const char *arg;

  va_start (args, podman);

  while ((arg = va_arg(args, char *)) != NULL)
    quad_podman_add (podman, arg);

  va_end (args);
}

void
quad_podman_add_array (QuadPodman *podman,
                       const char **strv,
                       gsize len)
{
  for (gsize i = 0; i < len; i++)
    quad_podman_add (podman, strv[i]);
}


static int
cmpstringp (const void *p1, const void *p2)
{
  return strcmp (*(char * const *) p1, *(char * const *) p2);
}

static void
quad_podman_add_keys (QuadPodman *podman,
                      const char *arg,
                      GHashTable *keys_ht)
{
  g_autofree char **keys = (char **)g_hash_table_get_keys_as_array (keys_ht, NULL);
  qsort (keys, g_strv_length (keys), sizeof (const char *), cmpstringp);
  for (guint i = 0; keys[i] != NULL; i++)
    {
      const char *key = keys[i];
      const char *value = g_hash_table_lookup (keys_ht, key);
      if (value)
        {
          quad_podman_add (podman, arg);
          quad_podman_addf (podman, "%s=%s", key, value);
        }
    }
}

void
quad_podman_add_env (QuadPodman *podman,
                     GHashTable *env)
{
  quad_podman_add_keys (podman, "--env", env);
}

void
quad_podman_add_labels (QuadPodman *podman,
                        GHashTable *labels)
{
  quad_podman_add_keys (podman, "--label", labels);
}

void
quad_podman_add_annotations (QuadPodman *podman,
                             GHashTable *annotations)
{
  quad_podman_add_keys (podman, "--annotation", annotations);
}

char *
quad_podman_to_exec (QuadPodman *podman)
{
  return quad_escape_words (podman->args);
}
