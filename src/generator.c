#include "quadlet-config.h"

#include <glib-object.h>
#include <unitfile.h>
#include <podman.h>
#include <utils.h>
#include <locale.h>
#include <stdint.h>
#include <unistd.h>

#define UNIT_GROUP "Unit"
#define SERVICE_GROUP "Service"
#define CONTAINER_GROUP "Container"
#define X_CONTAINER_GROUP "X-Container"
#define VOLUME_GROUP "Volume"
#define X_VOLUME_GROUP "X-Volume"


static const char *supported_container_keys[] = {
  "Image",
  "Environment",
  "Exec",
  "NoNewPrivileges",
  "DropCapability",
  "AddCapability",
  "RemapUsers",
  "RemapUidStart",
  "RemapGidStart",
  "RemapUidRanges",
  "RemapGidRanges",
  "Notify",
  "SocketActivated",
  "ExposeHostPort",
  "PublishPort",
  "KeepUser",
  "User",
  "Group",
  "HostUser",
  "HostGroup",
  "Volume",
  "PodmanArgs",
  "Label",
  "Annotation",
  "RunInit",
  "VolatileTmp",
  "Timezone",
  NULL
};
static GHashTable *supported_container_keys_hash = NULL;

static const char *supported_volume_keys[] = {
  "User",
  "Group",
  "Label",
  NULL
};
static GHashTable *supported_volume_keys_hash = NULL;

static QuadRanges *default_remap_uids = NULL;
static QuadRanges *default_remap_gids = NULL;

static gboolean quad_is_user = FALSE;

const char *default_drop_caps[] = {
  "all",
  NULL
};

static void
warn_for_unknown_keys (QuadUnitFile *unit,
                       const char *group_name,
                       const char **supported_keys,
                       GHashTable **supported_hash_p)
{
  g_autofree const char **keys = quad_unit_file_list_keys (unit, group_name);
  g_autoptr(GHashTable) warned = NULL;

  if (*supported_hash_p == NULL)
    {
      *supported_hash_p = g_hash_table_new (g_str_hash, g_str_equal);
      for (guint i = 0; supported_keys[i] != NULL; i++)
        g_hash_table_add (*supported_hash_p, (char *)supported_keys[i]);
    }

  for (guint i = 0; keys[i] != NULL; i++)
    {
      const char *key = keys[i];
      if (!g_hash_table_contains (*supported_hash_p, key))
        {
          if (warned == NULL)
            warned = g_hash_table_new (g_str_hash, g_str_equal);
          if (!g_hash_table_contains (warned, key))
            {
              quad_log ("Unsupported key '%s' in group '%s' in %s", key, group_name, quad_unit_file_get_path (unit));
              g_hash_table_add (warned, (char *)key);
            }
        }
    }
}

static void
parse_key_val (GHashTable *out,
               const char *env_val)
{
  char *eq = strchr (env_val, '=');
  if (eq != NULL)
    g_hash_table_insert (out, g_strndup (env_val, eq - env_val), g_strdup (eq+1));
  else
    quad_log ("Invalid key=value assignment '%s'", env_val);
}

static GHashTable *
parse_keys (char **key_vals)
{
  GHashTable *res = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  for (int i = 0 ; key_vals[i] != NULL; i++)
    {
      g_autoptr(GPtrArray) assigns = quad_split_string (key_vals[i], WHITESPACE, QUAD_SPLIT_RELAX|QUAD_SPLIT_UNQUOTE|QUAD_SPLIT_CUNESCAPE);
      for (guint j = 0; j < assigns->len; j++)
        parse_key_val (res, g_ptr_array_index (assigns, j));
    }
  return res;
}

static void
add_id_map (QuadPodman *podman,
            const char *arg_prefix,
            guint32 container_id_start,
            guint32 host_id_start,
            guint32 num_ids)
{
  if (num_ids != 0)
    {
      quad_podman_add (podman, arg_prefix);
      quad_podman_addf (podman, "%"G_GUINT32_FORMAT":%"G_GUINT32_FORMAT":%"G_GUINT32_FORMAT, container_id_start, host_id_start, num_ids);
    }
}

static void
add_id_maps (QuadPodman *podman,
             const char *arg_prefix,
             guint32 container_id,
             guint32 host_id,
             guint32 remap_start_id,
             QuadRanges *available_host_ids)
{
  g_autoptr(QuadRanges) unmapped_ids = NULL;
  g_autoptr(QuadRanges) mapped_ids = NULL;
  g_autoptr(QuadRanges) no_uids = NULL;

  if (available_host_ids == NULL)
    {
      /* Map everything by default */
      no_uids = quad_ranges_new_empty ();
      available_host_ids = no_uids;
    }

  /* Map the first ids up to remap_start_id to the host equivalent */
  unmapped_ids = quad_ranges_new (0, remap_start_id);

  /* The rest we want to map to available_host_ids. Note that this
   * overlaps unmapped_ids, because below we may remove ranges from
   * unmapped ids and we want to backfill those. */
  mapped_ids = quad_ranges_new (0, UINT32_MAX);

  /* Always map specified uid to specified host_uid */
  add_id_map (podman, arg_prefix, container_id, host_id, 1);

  /* We no longer want to map this container id as its already mapped*/
  quad_ranges_remove (mapped_ids, container_id, 1);
  quad_ranges_remove (unmapped_ids, container_id, 1);

  /* But also, we don't want to use the *host* id again, as we can only map it once  */
  quad_ranges_remove (unmapped_ids, host_id, 1);
  quad_ranges_remove (available_host_ids, host_id, 1);

  /* Map unmapped ids to equivalent host range, and remove from mapped_ids to avoid double-mapping */
  for (guint idx = 0; idx < unmapped_ids->n_ranges; idx++)
    {
      QuadRange *range = &unmapped_ids->ranges[idx];
      guint32 start = range->start;
      guint32 length = range->length;

      add_id_map (podman, arg_prefix, start, start, length);
      quad_ranges_remove (mapped_ids, start, length);
      quad_ranges_remove (available_host_ids, start, length);
    }

  for (guint c_idx = 0; c_idx < mapped_ids->n_ranges && available_host_ids->n_ranges > 0; c_idx++)
    {
      QuadRange *c_range = &mapped_ids->ranges[c_idx];
      guint32 c_start = c_range->start;
      guint32 c_length = c_range->length;

      while (c_length > 0 && available_host_ids->n_ranges > 0)
        {
          QuadRange *h_range = &available_host_ids->ranges[0];
          guint32 h_start = h_range->start;
          guint32 h_length = h_range->length;

          guint32 next_length = MIN (h_length, c_length);

          add_id_map (podman, arg_prefix, c_start, h_start, next_length);
          quad_ranges_remove (available_host_ids, h_start, next_length);
          c_start += next_length;
          c_length -= next_length;
        }
    }
}

static gboolean
is_port_range (const char *port)
{
  return g_regex_match_simple ("\\d+(-\\d+)?(/udp|/tcp)?$", port, G_REGEX_DOLLAR_ENDONLY, G_REGEX_MATCH_ANCHORED);
}

static QuadUnitFile *
convert_container (QuadUnitFile *container, GError **error)
{
  g_autoptr(QuadUnitFile) service =  quad_unit_file_copy (container);

  /* Rename old Container group to x-Container so that systemd ignores it */
  quad_unit_file_rename_group (service, CONTAINER_GROUP, X_CONTAINER_GROUP);

  warn_for_unknown_keys (container, CONTAINER_GROUP, supported_container_keys, &supported_container_keys_hash);

  g_autofree char *image = quad_unit_file_lookup (container, CONTAINER_GROUP, "Image");
  if (image == NULL || image[0] == 0)
    {
      quad_fail (error, "No Image key specified");
      return NULL;
    }

  /* Set PODMAN_SYSTEMD_UNIT so that podman auto-update can restart the service. */
  quad_unit_file_add (service, SERVICE_GROUP,
                      "Environment", "PODMAN_SYSTEMD_UNIT=%n");

  /* Only allow mixed or control-group, as nothing else works well */
  g_autofree char *kill_mode = quad_unit_file_lookup (service, SERVICE_GROUP, "KillMode");
  if (kill_mode == NULL ||
      !(strcmp (kill_mode, "mixed") == 0 ||
        strcmp (kill_mode, "control-group") == 0))
    {
      if (kill_mode != NULL)
        quad_log ("Invalid KillMode '%s', ignoring", kill_mode);

      /* We default to mixed instead of control-group, because it lets conmon do its thing */
      quad_unit_file_set (service, SERVICE_GROUP, "KillMode", "mixed");
    }

  /* Read env early so we can override it below */
  g_auto(GStrv) environments = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "Environment");
  g_autoptr(GHashTable) podman_env = parse_keys (environments);

  /* Need the containers filesystem mounted to start podman */
  quad_unit_file_add (service, UNIT_GROUP,
                      "RequiresMountsFor", "%t/containers");

  /* Remove any leftover cid file before starting, just to be sure.
   * We remove any actual pre-existing container by name with --replace=true.
   * But --cidfile will fail if the target exists. */
  quad_unit_file_add (service, SERVICE_GROUP,
                      "ExecStartPre", "-rm -f %t/%N.cid");

  /* If the conman exited uncleanly it may not have removed the container, so force it,
   * -i makes it ignore non-existing files. */
  quad_unit_file_add (service, SERVICE_GROUP,
                      "ExecStopPost", "-/usr/bin/podman rm -f -i --cidfile=%t/%N.cid");

  /* Remove the cid file, to avoid confusion as the container is no longer running. */
  quad_unit_file_add (service, SERVICE_GROUP,
                      "ExecStopPost", "-rm -f %t/%N.cid");

  g_autoptr(QuadPodman) podman = quad_podman_new ();

  quad_podman_addv (podman, "run",

                    /* We want to name the container by the service name */
                    "--name=systemd-%N",

                    /* We store the container id so we can clean it up in case of failure */
                    "--cidfile=%t/%N.cid",

                    /* And replace any previous container with the same name, not fail */
                    "--replace",

                    /* On clean shutdown, remove container */
                    "--rm",

                    /* Detach from container, we don't need the podman process to hang around */
                    "-d",

                    /* But we still want output to the journal, so use the log driver.
                     * TODO: Once available we want to use the passthrough log-driver instead. */
                    "--log-driver", "journald",

                    /* Never try to pull the image during service start */
                    "--pull=never",

                    NULL);

  /* We use crun as the runtime and delegated groups to it */
  quad_unit_file_add (service, SERVICE_GROUP, "Delegate", "yes");
  quad_podman_addv (podman,
                    "--runtime", "/usr/bin/crun",
                    "--cgroups=split",
                    NULL);

  g_autofree char *timezone =  quad_unit_file_lookup (container, CONTAINER_GROUP, "Timezone");
  /* Use the host timezone by default*/
  if (timezone == NULL)
    timezone = g_strdup ("local");
  if (*timezone != 0)
    quad_podman_addf (podman, "--tz=%s", timezone);

  /* Run with a pid1 init to reap zombies by default (as most apps don't do that) */
  gboolean run_init = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "RunInit", TRUE);
  if (run_init)
    quad_podman_addv (podman, "--init", NULL);


  /* By default we handle startup notification with conmon, but allow passing it to the container with Notify=yes */
  gboolean notify = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "Notify", FALSE);
  if (notify)
    quad_podman_add (podman, "--sdnotify=container");
  else
    quad_podman_add (podman, "--sdnotify=conmon");
  quad_unit_file_setv (service, SERVICE_GROUP,
                       "Type", "notify",
                       "NotifyAccess", "all",
                       NULL);

  if (!quad_unit_file_has_key (container, SERVICE_GROUP, "SyslogIdentifier"))
    quad_unit_file_set (service, SERVICE_GROUP, "SyslogIdentifier", "%N");

  /* Default to no higher level privileges or caps */
  gboolean no_new_privileges = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "NoNewPrivileges", TRUE);
  if (no_new_privileges)
    quad_podman_addv (podman, "--security-opt=no-new-privileges", NULL);

  const char **drop_caps;
  g_auto(GStrv) drop_caps_opts = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "DropCapability");
  if (quad_unit_file_has_key (container, CONTAINER_GROUP, "DropCapability"))
    drop_caps = (const char **)drop_caps_opts;
  else
    drop_caps = default_drop_caps;
  for (guint i = 0; drop_caps[i] != NULL; i++)
    {
      g_autofree char *caps = g_strdup (drop_caps[i]);
      for (guint j = 0; caps[j] != 0; j++)
        caps[j] = g_ascii_tolower (caps[j]);
      quad_podman_addf (podman, "--cap-drop=%s", caps);
    }

  /* But allow overrides with AddCapability*/
  g_auto(GStrv) add_caps = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "AddCapability");
  for (guint i = 0; add_caps[i] != NULL; i++)
    {
      g_autofree char *caps = g_strdup (add_caps[i]);
      for (guint j = 0; caps[j] != 0; j++)
        caps[j] = g_ascii_tolower (caps[j]);
      quad_podman_addf (podman, "--cap-add=%s", caps);
    }

  /* We want /tmp to be a tmpfs, like on rhel host */
  gboolean volatile_tmp = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "VolatileTmp", TRUE);
  if (volatile_tmp)
    quad_podman_addv (podman, "--mount", "type=tmpfs,tmpfs-size=512M,destination=/tmp", NULL);

  gboolean socket_activated = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "SocketActivated", FALSE);
  if (socket_activated)
    {
      /* TODO: This will not be needed with later podman versions that support activation directly:
       *  https://github.com/containers/podman/pull/11316  */
      quad_podman_add (podman, "--preserve-fds=1");
      g_hash_table_insert (podman_env, g_strdup ("LISTEN_FDS"), g_strdup ("1"));

      /* TODO: This will not be 2 when catatonit forwards fds:
       *  https://github.com/openSUSE/catatonit/pull/15 */
      g_hash_table_insert (podman_env, g_strdup ("LISTEN_PID"), g_strdup ("2"));
    }

  uid_t default_container_uid = 0;
  gid_t default_container_gid = 0;

  gboolean keep_id = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "KeepId", FALSE);
  if (keep_id)
    {
      if (quad_is_user)
        {
          default_container_uid = getuid ();
          default_container_gid = getgid ();
          quad_podman_addv (podman, "--userns", "keep-id", NULL);
        }
      else
        {
          keep_id = FALSE;
          quad_log ("Key 'KeepId' in '%s' unsupported for system units, ignoring", quad_unit_file_get_path (container));
        }
    }

  uid_t uid = MAX (quad_unit_file_lookup_int (container, CONTAINER_GROUP, "User", default_container_uid), 0);
  gid_t gid = MAX (quad_unit_file_lookup_int (container, CONTAINER_GROUP, "Group", default_container_gid), 0);

  uid_t host_uid = quad_unit_file_lookup_uid (container,CONTAINER_GROUP, "HostUser", uid, error);
  if (host_uid == (uid_t)-1)
    return NULL;

  gid_t host_gid = quad_unit_file_lookup_gid (container,CONTAINER_GROUP, "HostGroup", gid, error);
  if (host_gid == (gid_t)-1)
    return NULL;

  if (uid != default_container_uid || gid != default_container_uid)
    {
      quad_podman_add (podman, "--user");
      if (gid == default_container_gid)
        quad_podman_addf (podman, "%lu", (long unsigned)uid);
      else
        quad_podman_addf (podman, "%lu:%lu", (long unsigned)uid, (long unsigned)gid);
    }

  gboolean remap_users = quad_unit_file_lookup_boolean (container, CONTAINER_GROUP, "RemapUsers", TRUE);

  if (quad_is_user)
    remap_users = FALSE;

  if (!remap_users)
    {
      /* No remapping of users, although we still need maps if the
         main user/group is remapped, even if most ids map one-to-one. */
      if (uid != host_uid)
        add_id_maps (podman, "--uidmap",
                     uid, host_uid, UINT32_MAX, NULL);
      if (gid != host_gid)
        add_id_maps (podman, "--gidmap",
                     gid, host_gid, UINT32_MAX, NULL);
    }
  else
    {
      g_autoptr(QuadRanges) uid_remap_ids = quad_unit_file_lookup_ranges (container, CONTAINER_GROUP, "RemapUidRanges",
                                                                          quad_lookup_host_subuid, default_remap_uids);
      g_autoptr(QuadRanges) gid_remap_ids = quad_unit_file_lookup_ranges (container, CONTAINER_GROUP, "RemapGidRanges",
                                                                          quad_lookup_host_subgid, default_remap_gids);
      guint32 remap_uid_start = MAX (quad_unit_file_lookup_int (container, CONTAINER_GROUP, "RemapUidStart", 1), 0);
      guint32 remap_gid_start = MAX (quad_unit_file_lookup_int (container, CONTAINER_GROUP, "RemapGidStart", 1), 0);

      add_id_maps (podman, "--uidmap",
                   uid, host_uid,
                   remap_uid_start, uid_remap_ids);
      add_id_maps (podman, "--gidmap",
                   gid, host_gid,
                   remap_gid_start, gid_remap_ids);
    }

  g_auto(GStrv) volumes = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "Volume");
  for (guint i = 0; volumes[i] != NULL; i++)
    {
      const char *volume = volumes[i];
      char *source, *dest, *options = NULL;
      g_autofree char *volume_name = NULL;
      g_autofree char *volume_service_name = NULL;

      g_auto(GStrv) parts = g_strsplit (volume, ":", 3);
      if (g_strv_length (parts) < 2)
        {
          quad_log ("Ignoring invalid volume %s", volume);
          continue;
        }
      source = parts[0];
      dest = parts[1];
      if (g_strv_length (parts) >= 3)
        options = parts[2];

      if (source[0] == '/')
        {
          /* Absolute path */
          quad_unit_file_add (service, UNIT_GROUP,
                              "RequiresMountsFor", source);
        }
      else
        {
          /* unit name (with .volume suffix) or named podman volume */

          if (g_str_has_suffix (source, ".volume"))
            {
              /* the podman volume name is systemd-$name */
              volume_name = quad_replace_extension (source, NULL, "systemd-", NULL);

              /* the systemd unit name is $name-volume.service */
              volume_service_name = quad_replace_extension (source, ".service", NULL, "-volume");

              source = volume_name;

              quad_unit_file_add (service, UNIT_GROUP,
                                  "Requires", volume_service_name);
              quad_unit_file_add (service, UNIT_GROUP,
                                  "After", volume_service_name);
            }
        }

      quad_podman_add (podman, "-v");
      quad_podman_addf (podman, "%s:%s%s%s", source, dest, options ? ":" : "", options ? options : "");
    }

  g_auto(GStrv) exposed_ports = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "ExposeHostPort");
  for (guint i = 0; exposed_ports[i] != NULL; i++)
    {
      char *exposed_port = g_strchomp (exposed_ports[i]); /* Allow whitespace after */

      if (!is_port_range (exposed_port))
        {
          quad_log ("Invalid port format '%s'", exposed_port);
          continue;
        }

      quad_podman_addf (podman, "--expose=%s", exposed_port);
    }

  g_auto(GStrv) publish_ports = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "PublishPort");
  for (guint i = 0; publish_ports[i] != NULL; i++)
    {
      char *publish_port = g_strchomp (publish_ports[i]); /* Allow whitespace after */
      g_auto(GStrv) parts = g_strsplit (publish_port, ":", -1);
      const char *container_port = NULL, *ip = NULL, *host_port = NULL;

      /* format (from podman run): ip:hostPort:containerPort | ip::containerPort | hostPort:containerPort | containerPort */

      switch (g_strv_length (parts))
        {
        case 1:
          container_port = parts[0];
          break;

        case 2:
          host_port = parts[0];
          container_port = parts[1];
          break;

        case 3:
          ip = parts[0];
          host_port = parts[1];
          container_port = parts[2];
          break;

        default:
          quad_log ("Ignoring invalid published port '%s'", publish_port);
          continue;
        }

      if (host_port && *host_port == 0)
        host_port = NULL;

      if (ip && (strcmp (ip, "0.0.0.0") == 0 || *ip == 0))
        ip = NULL;

      if (host_port && !is_port_range (host_port))
        {
          quad_log ("Invalid port format '%s'", host_port);
          continue;
        }

      if (container_port && !is_port_range (container_port))
        {
          quad_log ("Invalid port format '%s'", container_port);
          continue;
        }

      if (ip)
        quad_podman_addf (podman, "-p=%s:%s:%s", ip, host_port ? host_port : "", container_port);
      else if (host_port)
        quad_podman_addf (podman, "-p=%s:%s", host_port, container_port);
      else
        quad_podman_addf (podman, "-p=%s", container_port);
    }

  quad_podman_add_env (podman, podman_env);

  g_auto(GStrv) labels = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "Label");
  g_autoptr(GHashTable) podman_labels = parse_keys (labels);
  quad_podman_add_labels (podman, podman_labels);

  g_auto(GStrv) annotations = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "Annotation");
  g_autoptr(GHashTable) podman_annotations = parse_keys (annotations);
  quad_podman_add_annotations (podman, podman_annotations);

  g_auto(GStrv) podman_argsv = quad_unit_file_lookup_all (container, CONTAINER_GROUP, "PodmanArgs");
  for (guint i = 0; podman_argsv[i] != NULL; i++)
    {
      char *podman_args_s = podman_argsv[i];
      g_autoptr(GPtrArray) podman_args = quad_split_string (podman_args_s, WHITESPACE,
                                                            QUAD_SPLIT_RELAX|QUAD_SPLIT_UNQUOTE);
      quad_podman_add_array (podman, (const char **)podman_args->pdata, podman_args->len);
    }

  quad_podman_add (podman, image);

  g_autofree char *exec_key = quad_unit_file_lookup_last (container, CONTAINER_GROUP, "Exec");
  if (exec_key != NULL)
    {
      g_autoptr(GPtrArray) exec_args = quad_split_string (exec_key, WHITESPACE,
                                                          QUAD_SPLIT_RELAX|QUAD_SPLIT_UNQUOTE);
      quad_podman_add_array (podman, (const char **)exec_args->pdata, exec_args->len);
    }

  g_autofree char *exec_start = quad_podman_to_exec (podman);
  quad_unit_file_add (service, SERVICE_GROUP, "ExecStart", exec_start);

  return g_steal_pointer (&service);
}

static QuadUnitFile *
convert_volume (QuadUnitFile *container,
                const char *name,
                G_GNUC_UNUSED GError **error)
{
  g_autoptr(QuadUnitFile) service =  quad_unit_file_copy (container);
  g_autofree char *volume_name = quad_replace_extension (name, NULL, "systemd-", NULL);

  warn_for_unknown_keys (container, VOLUME_GROUP, supported_volume_keys, &supported_volume_keys_hash);

  /* Rename old Volume group to x-Volume so that systemd ignores it */
  quad_unit_file_rename_group (service, VOLUME_GROUP, X_VOLUME_GROUP);

  /* Need the containers filesystem mounted to start podman */
  quad_unit_file_add (service, UNIT_GROUP,
                      "RequiresMountsFor", "%t/containers");

  g_autofree char *exec_cond = g_strdup_printf ("/usr/bin/bash -c \"! /usr/bin/podman volume exists %s\"", volume_name);

  g_auto(GStrv) labels = quad_unit_file_lookup_all (container, VOLUME_GROUP, "Label");
  g_autoptr(GHashTable) podman_labels = parse_keys (labels);

  g_autoptr(QuadPodman) podman = quad_podman_new ();
  quad_podman_addv (podman,
                    "volume", "create", NULL);

  g_autoptr(GString) opts = g_string_new ("o=");

  if (quad_unit_file_has_key (container, VOLUME_GROUP, "User"))
    {
      long uid = MAX (quad_unit_file_lookup_int (container, VOLUME_GROUP, "User", 0), 0);
      if (opts->len > 2)
        g_string_append (opts, ",");
      g_string_append_printf (opts, "uid=%ld", uid);
    }

  if (quad_unit_file_has_key (container, VOLUME_GROUP, "Group"))
    {
      long gid = MAX (quad_unit_file_lookup_int (container, VOLUME_GROUP, "Group", 0), 0);
      if (opts->len > 2)
        g_string_append (opts, ",");
      g_string_append_printf (opts, "gid=%ld", gid);
    }

  if (opts->len > 2)
    quad_podman_addv (podman, "--opt", opts->str, NULL);

  quad_podman_add_labels (podman, podman_labels);
  quad_podman_add (podman,volume_name);

  g_autofree char *exec_start = quad_podman_to_exec (podman);

  quad_unit_file_setv (service, SERVICE_GROUP,
                       "Type", "oneshot",
                       "RemainAfterExit", "yes",
                       "ExecCondition", exec_cond,
                       "ExecStart", exec_start,

                       /* The default syslog identifier is the exec basename (podman) which isn't very useful here */
                       "SyslogIdentifier", "%N",
                       NULL);

  return g_steal_pointer (&service);
}

static void
generate_service_file (const char *output_path,
                       QuadUnitFile *service,
                       const char *orig_filename,
                       const char *extra_suffix,
                       QuadUnitFile *orig_unit)
{
  g_autoptr(GString) str = g_string_new ("");
  g_autoptr(GError) error = NULL;
  const char *orig_path = quad_unit_file_get_path (orig_unit);
  g_autofree char *service_name = quad_replace_extension (orig_filename, ".service", NULL, extra_suffix);
  g_autofree char *out_filename = g_build_filename (output_path, service_name, NULL);

  g_string_append (str, "# Automatically generated by quadlet-generator\n");
  if (orig_path)
    quad_unit_file_add (service, UNIT_GROUP,
                        "SourcePath", orig_path);
  quad_unit_file_print (service, str);

  quad_debug ("writing '%s'", out_filename);
  if (!g_file_set_contents (out_filename, str->str, str->len, &error))
    quad_log ("Error writing '%s', ignoring: %s", out_filename, error->message);
}

static void
load_units_from_dir (const char *source_path,
                     GHashTable *units)
{
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GError) dir_error = NULL;
  const char *name;

  dir = g_dir_open (source_path, 0, &dir_error);
  if (dir == NULL)
    {
      if (!g_error_matches (dir_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        quad_log ("Can't read \"%s\": %s", source_path, dir_error->message);
      return;
    }

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      if ((g_str_has_suffix (name, ".container") ||
           g_str_has_suffix (name, ".volume")) &&
          !g_hash_table_contains (units, name))
        {
          g_autofree char *path = g_build_filename (source_path, name, NULL);
          g_autoptr(QuadUnitFile) unit = NULL;
          g_autoptr(GError) error = NULL;

          quad_debug ("Loading source unit file %s", path);

          unit = quad_unit_file_new_from_path (path, &error);
          if (unit == NULL)
            quad_log ("Error loading '%s', ignoring: %s", path, error->message);
          else
            g_hash_table_insert (units, g_strdup (name), g_steal_pointer (&unit));
        }
    }
}

static gboolean opt_verbose;
static gboolean opt_version;

static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information", NULL },
  { "version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print version information and exit", NULL },
  { NULL }
};

int
main (int argc,
      char **argv)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GHashTable) units = NULL;
  g_autoptr(GError) error = NULL;
  const char *output_path;
  const char **source_paths;
  g_autofree char *prgname = NULL;

  setlocale (LC_ALL, "");

  prgname = g_path_get_basename (argv[0]);
  g_set_prgname (prgname);

  quad_is_user = strstr (prgname, "user") != NULL;

  context = g_option_context_new ("OUTPUTDIR - Generate service files");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      quad_log ("Option parsing failed: %s\n", error->message);
      return 1;
    }

  if (opt_version)
    {
      g_print ("quadlet %s\n", PACKAGE_VERSION);
      return 0;
    }

  if (opt_verbose)
    quad_enable_debug ();

  if (argc < 2)
    {
      quad_log ("Missing output directory argument");
      return 1;
    }

  output_path = argv[1];

  quad_debug ("Starting quadlet-generator, output to: %s", output_path);

  default_remap_uids = quad_lookup_host_subuid (QUADLET_USERNAME);
  if (default_remap_uids == NULL) /* Fall back to built-in default */
    default_remap_uids = quad_ranges_new (QUADLET_FALLBACK_UID_START, QUADLET_FALLBACK_UID_LENGTH);

  default_remap_gids = quad_lookup_host_subgid (QUADLET_USERNAME);
  if (default_remap_gids == NULL) /* Fall back to built-in default */
    default_remap_gids = quad_ranges_new (QUADLET_FALLBACK_GID_START, QUADLET_FALLBACK_GID_LENGTH);

  source_paths = quad_get_unit_dirs (quad_is_user);

  units = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  for (guint i = 0; source_paths[i] != NULL; i++)
    load_units_from_dir (source_paths[i], units);

  QUAD_HASH_TABLE_FOREACH_KV (units, const char*, name, QuadUnitFile *, unit)
    {
      g_autoptr(QuadUnitFile) service = NULL;
      g_autoptr(GError) error = NULL;
      const char *extra_suffix = NULL;

      if (g_str_has_suffix (name, ".container"))
        {
          service = convert_container (unit, &error);
        }
      else if (g_str_has_suffix (name, ".volume"))
        {
          service = convert_volume (unit, name, &error);
          extra_suffix = "-volume";
        }
      else
        {
          quad_log ("Unsupported type '%s'", name);
          continue;
        }

      if (service == NULL)
        quad_log ("Error converting '%s', ignoring: %s", name, error->message);
      else
        generate_service_file (output_path, service, name, extra_suffix, unit);
    }

  return 0;
}
