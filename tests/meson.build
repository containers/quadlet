tests_environment = environment()
tests_environment.set('G_TEST_SRCDIR', meson.current_source_dir())
tests_environment.set('G_TEST_BUILDDIR', meson.current_build_dir())
tests_environment.set('LC_ALL', 'C')

test_utils =  executable('test-utils', 'test-utils.c',
                            include_directories: src_inc,
                            link_with: libquadlet,
                            dependencies: libquadlet_dep)

test('utils test', test_utils, env: tests_environment)

testcase_runner = find_program ('testcase-runner.py')
test('generator', testcase_runner,
     args: [join_paths(meson.current_source_dir(), 'cases'),
            quadlet_generator.path()])
