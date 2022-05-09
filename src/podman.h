#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct QuadPodman QuadPodman;

QuadPodman *quad_podman_new (const char *command,
                             const char *sub_command);
void        quad_podman_free (QuadPodman *podman);
void        quad_podman_add (QuadPodman *podman,
                             const char *arg);
void        quad_podman_addf (QuadPodman *podman,
                              const char *fmt,
                              ...) G_GNUC_PRINTF (2, 3);
void        quad_podman_addv (QuadPodman *podman,
                              ...) G_GNUC_NULL_TERMINATED;
void        quad_podman_add_array (QuadPodman *podman,
                                   const char **strv,
                                   gsize len);
void        quad_podman_add_env (QuadPodman *podman,
                                 GHashTable *envs);
void        quad_podman_add_labels (QuadPodman *podman,
                                    GHashTable *labels);
void        quad_podman_add_annotations (QuadPodman *podman,
                                         GHashTable *annotations);
char *      quad_podman_to_exec (QuadPodman *podman);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (QuadPodman, quad_podman_free)

G_END_DECLS
