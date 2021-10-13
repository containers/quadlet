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

meson.add_install_script('mkdir', '-p',
                         '$DESTDIR' / quadlet_user_generatordir,
                         '$DESTDIR' / quadlet_system_generatordir)
meson.add_install_script('ln', '-sfr', '$DESTDIR' / quadlet_generator_installed_path, '$DESTDIR' / quadlet_user_generatordir  / 'quadlet-user-generator')
meson.add_install_script('ln', '-sfr', '$DESTDIR' / quadlet_generator_installed_path, '$DESTDIR' / quadlet_user_generatordir  / 'quadlet-system-generator')
