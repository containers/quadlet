#include <glib-object.h>
#include <unitfile.h>
#include <locale.h>


int
main (int argc, char *argv[])
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  // Define the tests.
  g_test_add_func ("/ranges/creation", test_creation);
  g_test_add_func ("/ranges/single", test_single);

  return g_test_run ();
}
