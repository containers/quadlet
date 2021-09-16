#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define QUAD_TYPE_UNIT_FILE (quad_unit_file_get_type())

G_DECLARE_FINAL_TYPE (QuadUnitFile, quad_unit_file, QUAD, UNIT_FILE, GObject)

QuadUnitFile *quad_unit_file_new_from_path (const char  *path,
                                            GError     **error);
QuadUnitFile *quad_unit_file_new           (void);

void          quad_unit_file_merge           (QuadUnitFile  *self,
                                              QuadUnitFile  *source);
QuadUnitFile *quad_unit_file_copy            (QuadUnitFile  *self);
gboolean      quad_unit_file_parse           (QuadUnitFile  *self,
                                              const char    *data,
                                              GError       **error);
void          quad_unit_file_print           (QuadUnitFile  *self,
                                              GString       *str);
const char *  quad_unit_file_lookup_last_raw (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key);
char *        quad_unit_file_lookup_last     (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key);
gboolean      quad_unit_file_lookup_boolean  (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key,
                                              gboolean       default_value);
long          quad_unit_file_lookup_int      (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key,
                                              long           default_value);
const char ** quad_unit_file_lookup_all_raw  (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key);
char **       quad_unit_file_lookup_all      (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key);
gboolean      quad_unit_file_has_group       (QuadUnitFile  *self,
                                              const char    *group_name);
const char ** quad_unit_file_list_groups     (QuadUnitFile  *self);
const char ** quad_unit_file_list_keys       (QuadUnitFile  *self,
                                              const char    *group_name);
void          quad_unit_file_set             (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key,
                                              const char    *value);
void          quad_unit_file_add             (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key,
                                              const char    *value);
void          quad_unit_file_unset           (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *key);
void          quad_unit_file_remove_group    (QuadUnitFile  *self,
                                              const char    *group_name);
void          quad_unit_file_rename_group    (QuadUnitFile  *self,
                                              const char    *group_name,
                                              const char    *new_name);

G_END_DECLS
