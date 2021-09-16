#include <glib-object.h>
#include <unitfile.h>
#include <locale.h>

const char *sample_files[] = {
  "memcached.service",
  "systemd-logind.service",
  "systemd-networkd.service",
};

static char *
get_sample_path (const char *filename)
{
  return g_test_build_filename (G_TEST_DIST, "samples", filename, NULL);
}

static char *
load_sample_file (const char *filename)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *path = get_sample_path (filename);
  char *data;

  g_file_get_contents (path, &data, NULL, &error);
  g_assert_no_error (error);

  return data;
}


static QuadUnitFile *
load_sample_unit (const char *filename)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *path = get_sample_path (filename);
  QuadUnitFile *unit;

  unit = quad_unit_file_new_from_path (path, &error);
  g_assert_no_error (error);
  g_assert_true (unit != NULL);
  return unit;
}

/* Make sure we can reproduce some sample systemd unit files */
static void
test_print (void)
{
  for (guint i = 0; i < G_N_ELEMENTS (sample_files); i++)
    {
      const char *sample_file = sample_files[i];
      g_autofree char *data = load_sample_file (sample_file);
      g_autoptr(QuadUnitFile) unit = load_sample_unit (sample_file);
      g_autoptr(GString) str = g_string_new ("");
      quad_unit_file_print (unit, str);
      g_assert_cmpstr (str->str, ==, data);
    }
}

int
main (int argc, char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  // Define the tests.
  g_test_add_func ("/unit-file/print", test_print);

  return g_test_run ();
}
