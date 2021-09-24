project('quadlet', 'c',
          version: '0.1.0',
    meson_version: '>= 0.50.0',
  default_options: [ 'warning_level=2',
                     'c_std=gnu11',
                   ],
)

top_inc = include_directories('.')

quadlet_prefix = get_option('prefix')
quadlet_generatordir = join_paths(quadlet_prefix, quadlet_prefix, 'lib/systemd/system-generators')
quadlet_sysconfdir = join_paths(quadlet_prefix, get_option('sysconfdir'), 'containers/systemd')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('QUADLET_UNIT_DIR', quadlet_sysconfdir)
configure_file(
  output: 'quadlet-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.build_root(),
], language: 'c')

glib_dep       = dependency('glib-2.0', version: '>= 2.44')
gobject_dep    = dependency('gobject-2.0', version: '>= 2.44')

subdir('src')
subdir('tests')
