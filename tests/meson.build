tests_environment = environment()
tests_environment.set('G_TEST_SRCDIR', meson.current_source_dir())
tests_environment.set('G_TEST_BUILDDIR', meson.current_build_dir())
tests_environment.set('LC_ALL', 'C')

test_unitfile =  executable('test-unitfile', 'test-unitfile.c',
                            include_directories: src_inc,
                            link_with: libquadlet,
                            dependencies: libquadlet_dep)

test('unit-file test', test_unitfile, env: tests_environment)
