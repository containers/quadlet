src_inc = include_directories('.')

lib_sources = files(
  'unitfile.c',
  'unitfile.h',
  'podman.c',
  'podman.h',
  'utils.c',
  'utils.h',
)

libquadlet = static_library(
  'libquadlet',
  sources: lib_sources,
  include_directories: top_inc,
  dependencies: [glib_dep, gobject_dep],
)

libquadlet_dep = declare_dependency(
  dependencies: [glib_dep, gobject_dep],
  link_whole: libquadlet,
)

generator_sources = [
  'generator.c',
]

quadlet_generator = executable('quadlet-generator', generator_sources,
  dependencies: libquadlet_dep,
  install: true,
  install_dir : quadlet_libexecdir,
)

quadlet_generator_installed_path = join_paths(quadlet_libexecdir, 'quadlet-generator')

meson.add_install_script('sh', '-c', 'mkdir -p $DESTDIR@0@'.format(quadlet_user_generatordir))
meson.add_install_script('sh', '-c', 'ln -sfr $DESTDIR@0@ $DESTDIR@1@/quadlet-user-generator'.format(quadlet_generator_installed_path, quadlet_user_generatordir))
meson.add_install_script('sh', '-c', 'mkdir -p $DESTDIR@0@'.format(quadlet_system_generatordir))
meson.add_install_script('sh', '-c', 'ln -sfr $DESTDIR@0@ $DESTDIR@1@/quadlet-system-generator'.format(quadlet_generator_installed_path, quadlet_system_generatordir))
