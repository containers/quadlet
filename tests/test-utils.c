#include <glib-object.h>
#include <unitfile.h>
#include <utils.h>
#include <locale.h>

const char *sample_service_files[] = {
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
test_unitfile_print (void)
{
  for (guint i = 0; i < G_N_ELEMENTS (sample_service_files); i++)
    {
      const char *sample_file = sample_service_files[i];
      g_autofree char *data = load_sample_file (sample_file);
      g_autoptr(QuadUnitFile) unit = load_sample_unit (sample_file);
      g_autoptr(GString) str = g_string_new ("");
      quad_unit_file_print (unit, str);
      g_assert_cmpstr (str->str, ==, data);
    }
}

static void
test_range_creation (void)
{
  g_autoptr(QuadRanges) empty = quad_ranges_new_empty ();

  g_assert_cmpuint (empty->n_ranges, ==, 0);

  g_autoptr(QuadRanges) one = quad_ranges_new (17, 42);

  g_assert_cmpuint (one->n_ranges, ==, 1);
  g_assert_cmpuint (one->ranges[0].start, ==, 17);
  g_assert_cmpuint (one->ranges[0].length, ==, 42);
}

/* Test handling of a single range + another range, all cases */
static void
test_range_single (void)
{
  /* Before */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 0, 9);

    g_assert_cmpuint (r->n_ranges, ==, 2);
    g_assert_cmpuint (r->ranges[0].start, ==, 0);
    g_assert_cmpuint (r->ranges[0].length, ==, 9);
    g_assert_cmpuint (r->ranges[1].start, ==, 10);
    g_assert_cmpuint (r->ranges[1].length, ==, 10);
  }

  /* just before */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 0, 10);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 0);
    g_assert_cmpuint (r->ranges[0].length, ==, 20);
  }

  /* before + inside */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 0, 19);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 0);
    g_assert_cmpuint (r->ranges[0].length, ==, 20);
  }

  /* before + inside, whole */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 0, 20);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 0);
    g_assert_cmpuint (r->ranges[0].length, ==, 20);
  }

  /* before + inside + after */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 0, 30);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 0);
    g_assert_cmpuint (r->ranges[0].length, ==, 30);
  }

  /* just inside */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 10, 5);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 10);
  }

  /* inside */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 12, 5);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 10);
  }

  /* inside at end */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 15, 5);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 10);
  }

  /* inside + after */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 15, 10);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 15);
  }

  /* just after */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 20, 10);

    g_assert_cmpuint (r->n_ranges, ==, 1);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 20);
  }

  /* after */
  {
    g_autoptr(QuadRanges) r = quad_ranges_new (10, 10);

    quad_ranges_add (r, 21, 10);

    g_assert_cmpuint (r->n_ranges, ==, 2);
    g_assert_cmpuint (r->ranges[0].start, ==, 10);
    g_assert_cmpuint (r->ranges[0].length, ==, 10);
    g_assert_cmpuint (r->ranges[1].start, ==, 21);
    g_assert_cmpuint (r->ranges[1].length, ==, 10);
  }
}

static void
test_range_multi (void)
{
    g_autoptr(QuadRanges) base = quad_ranges_new (10, 10);
    quad_ranges_add (base, 50, 10);
    quad_ranges_add (base, 30, 10);

    /* Test copy */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      g_assert_cmpuint (r->n_ranges, ==, 3);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 30);
      g_assert_cmpuint (r->ranges[1].length, ==, 10);
      g_assert_cmpuint (r->ranges[2].start, ==, 50);
      g_assert_cmpuint (r->ranges[2].length, ==, 10);
    }

    /* overlap everything */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_add (r, 0, 100);

      g_assert_cmpuint (r->n_ranges, ==, 1);
      g_assert_cmpuint (r->ranges[0].start, ==, 0);
      g_assert_cmpuint (r->ranges[0].length, ==, 100);
    }

    /* overlap middle */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_add (r, 25, 10);

      g_assert_cmpuint (r->n_ranges, ==, 3);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 25);
      g_assert_cmpuint (r->ranges[1].length, ==, 15);
      g_assert_cmpuint (r->ranges[2].start, ==, 50);
      g_assert_cmpuint (r->ranges[2].length, ==, 10);
    }

    /* overlap last */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_add (r, 45, 10);

      g_assert_cmpuint (r->n_ranges, ==, 3);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 30);
      g_assert_cmpuint (r->ranges[1].length, ==, 10);
      g_assert_cmpuint (r->ranges[2].start, ==, 45);
      g_assert_cmpuint (r->ranges[2].length, ==, 15);
    }
}

static void
test_range_remove (void)
{
    g_autoptr(QuadRanges) base = quad_ranges_new (10, 10);
    quad_ranges_add (base, 50, 10);
    quad_ranges_add (base, 30, 10);

    /* overlap all */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_remove (r, 0, 100);

      g_assert_cmpuint (r->n_ranges, ==, 0);
    }

    /* overlap middle 1 */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_remove (r, 25, 20);

      g_assert_cmpuint (r->n_ranges, ==, 2);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 50);
      g_assert_cmpuint (r->ranges[1].length, ==, 10);
    }

    /* overlap middle 2 */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_remove (r, 25, 10);

      g_assert_cmpuint (r->n_ranges, ==, 3);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 35);
      g_assert_cmpuint (r->ranges[1].length, ==, 5);
      g_assert_cmpuint (r->ranges[2].start, ==, 50);
      g_assert_cmpuint (r->ranges[2].length, ==, 10);
    }

    /* overlap middle 3 */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_remove (r, 35, 10);

      g_assert_cmpuint (r->n_ranges, ==, 3);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 30);
      g_assert_cmpuint (r->ranges[1].length, ==, 5);
      g_assert_cmpuint (r->ranges[2].start, ==, 50);
      g_assert_cmpuint (r->ranges[2].length, ==, 10);
    }

    /* overlap middle 4 */
    {
      g_autoptr(QuadRanges) r = quad_ranges_copy (base);

      quad_ranges_remove (r, 34, 2);

      g_assert_cmpuint (r->n_ranges, ==, 4);
      g_assert_cmpuint (r->ranges[0].start, ==, 10);
      g_assert_cmpuint (r->ranges[0].length, ==, 10);
      g_assert_cmpuint (r->ranges[1].start, ==, 30);
      g_assert_cmpuint (r->ranges[1].length, ==, 4);
      g_assert_cmpuint (r->ranges[2].start, ==, 36);
      g_assert_cmpuint (r->ranges[2].length, ==, 4);
      g_assert_cmpuint (r->ranges[3].start, ==, 50);
      g_assert_cmpuint (r->ranges[3].length, ==, 10);
    }
}

static void
test_split_ports (void)
{
  {
    g_auto(GStrv) parts = quad_split_ports ("");

    g_assert (g_strv_length (parts) == 1);
    g_assert_cmpstr (parts[0], ==, "");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("foo");

    g_assert (g_strv_length (parts) == 1);
    g_assert_cmpstr (parts[0], ==, "foo");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("foo:bar");

    g_assert (g_strv_length (parts) == 2);
    g_assert_cmpstr (parts[0], ==, "foo");
    g_assert_cmpstr (parts[1], ==, "bar");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("foo:bar:");

    g_assert (g_strv_length (parts) == 3);
    g_assert_cmpstr (parts[0], ==, "foo");
    g_assert_cmpstr (parts[1], ==, "bar");
    g_assert_cmpstr (parts[2], ==, "");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("abc[foo::bar]xyz:foo:bar");

    g_assert (g_strv_length (parts) == 3);
    g_assert_cmpstr (parts[0], ==, "abc[foo::bar]xyz");
    g_assert_cmpstr (parts[1], ==, "foo");
    g_assert_cmpstr (parts[2], ==, "bar");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("foo:abc[foo::bar]xyz:bar");

    g_assert (g_strv_length (parts) == 3);
    g_assert_cmpstr (parts[0], ==, "foo");
    g_assert_cmpstr (parts[1], ==, "abc[foo::bar]xyz");
    g_assert_cmpstr (parts[2], ==, "bar");
  }

  {
    g_auto(GStrv) parts = quad_split_ports ("foo:abc[foo::barxyz:bar");

    g_assert (g_strv_length (parts) == 2);
    g_assert_cmpstr (parts[0], ==, "foo");
    g_assert_cmpstr (parts[1], ==, "abc[foo::barxyz:bar");
  }

}

int
main (int argc, char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  // Define the tests.
  g_test_add_func ("/unit-file/print", test_unitfile_print);
  g_test_add_func ("/ranges/creation", test_range_creation);
  g_test_add_func ("/ranges/single", test_range_single);
  g_test_add_func ("/ranges/multi", test_range_multi);
  g_test_add_func ("/ranges/remove", test_range_remove);
  g_test_add_func ("/split-ports", test_split_ports);

  return g_test_run ();
}
