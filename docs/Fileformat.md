# Quadlet file formats

The quadlet generator runs very early during boot, and then whenever
systemd reloads its configuration (as triggered by `systemctl
daemon-reload`). At this point it will read files from these
locations, in this order:

 * `/etc/containers/systemd`
 * `/usr/share/containers/systemd`

Any file found in one directory will shadow similarly named ones in
later directories. It is expected that the distribution will ship
packaged files under `/usr`, and the local system administrator puts
files under `/etc`.

In these directories quadlet reads files with the extensions
`.container` and `.volume`, defining podman containers and volumes
respectively. For each of thes quadlet will generate a similarly
named service file.

These files use the same format as [regular systemd unit
files](https://www.freedesktop.org/software/systemd/man/systemd.syntax.html). Each
file type has a custom section (for example `[Container]`) that is
handled by quadlet, and all other sections will be passed on untouched
to the generated systemd service file, so can contain any normal
systemd configuration. The custom section is also visible in the
generated file, but with a `X-` prefix which means systemd ignores it.

Quadlet also supports `systemd --user` unit. Any quadlet files stored
in `$XDG_CONFIG_HOME/containers/systemd` (default is
`~/.config/containers/systemd`) will be converted to user systemd
services. These work more or less the same as the regular system
units, with some exceptions when it comes to uid mappings (see below).

# Enabling unit files

The service files created by quadlet are considered "generated" by
systemd, so they don't have the same persistance rules as regular unit
files. In particular, it is not possible to "systemctl enable" them in
order for them to become automatically enabled on the next boot.

To compensate for this, the generator manually applies the `[Install]`
section of the container definition unit files during generation, in
the same way `systemctl enable` would do when run later.

For example, to start a container on boot, you can do something like:

```
[Install]
WantedBy=multi-user.target
```

Currently only the `Alias`, `WantedBy` and `RequiredBy` keys are supported.

NOTE: If you want to express dependencies between containers you need
to use the generated names of the service. In other words
`WantedBy=other.service`, not `WantedBy=other.container`. The same is
true for other kinds of dependencies too, like `After=other.service`.

# Container files

Container files are named with a `.container` extension and contain a
section `[Container]` describing the container that should be run as a
service. The resulting service file will contain a line like
`ExecStart=podman run … image-name`, and most of the keys in this
section control the commandline options passed to podman.  However,
some options also affect details of how systemd is set up to run and
interact with the container.

The podman container will have the same name as the unit, but with a
`systemd-` prefix. I.e. a `$name.container` file will create a
`$name.service` unit and a `systemd-$name` podman container.

There is only one required key, `Image` which defines the container
image that should be run by the service.

Supported keys in `Container` group are:

* `Image=`

   The image to run in the container. This image must be locally
   installed (as root) for the service to work when it is activated,
   because the generated service file will never try to download
   images.

* `Environment=`

  Set an environment variable in the container. This uses the same
  format as [services in systemd](https://www.freedesktop.org/software/systemd/man/systemd.exec.html#Environment=)
  and can be listed multiple times.

* `Exec=`

  If this is set then it defines what commandline to run in the container. If it
  is not set the default entry point of the container image is used.
  The format is the same as for [systemd command lines](https://www.freedesktop.org/software/systemd/man/systemd.service.html#Command%20lines).

* `User=`

  The (numeric) uid to run as inside the container. This does not need
  to match the uid on the host, which can be set with `HostUser`, but
  if that is not specified the uid is used on the host.

  Note that by default (unless `RemapUsers` is set to false) all other host
  users are unmapped in the container, and the user is run without any
  capabilities (even if uid is 0), any required capabilities must be
  granted with `AddCapability`.

* `HostUser=`

  The host uid (numeric or a username) to run the container as. If this
  differs from the uid in `User` then user namespaces are used to map
  the ids. If unspecified this defaults to what was specified in `User`.

* `Group=`

  The (numeric) gid to run as inside the container. This does not need
  to match the gid on the host, which can be set with `HostGroup`, but
  if that is not specified the same host gid is used.

* `HostGroup=`

  The host gid (numeric or group name) to run the container as. If
  this differs from the gid in `Group` then user namespaces are used
  to map the ids. If unspecified this defaults to what was specified in `Group`.

* `NoNewPrivileges=` (defaults to `yes`)

  If enabled (which is the default) this disables the container
  processes from gaining additional privileges via things like
  setuid and file capabilities.

* `DropCapability=` (defaults to `all`)

   Drop these capabilities from the default container capability set.
   The default is `all`, so you have to add any capabilities you want
   with `AddCapability`. Set this to empty to drop no capabilities.
   This can be listed multiple times.

* `AddCapability=`

   By default the container runs with no capabilities (due to
   DropCapabilities='all'), if any specific caps are needed you can
   list add them with this key. For example using
   `AddCapability=CAP_DAC_OVERRIDE`. This can be listed multiple
   times.

* `RemapUsers=` (defaults to `yes` for system units, always `no` on user units)

   If this is enabled (which is the default for system units), then
   host user and group ids are remapped in the container, such that
   all the uids starting at `RemapUidStart` (and gids starting at
   `RemapGidStart`) in the container are chosen from the available
   host uids specified by `RemapUidRanges` (and `RemapGidRanges`).

* `RemapUidStart=` (defaults to `1`)

   If `RemapUsers` is available, this is the first uid that is
   remapped, and all lower uids are mapped to the equivalent host
   uid. This defaults to 1, so that the host root uid is in the
   container, as this means a lot less file ownership remapping in the
   container image.

* `RemapGidStart=` (defaults to `1`)

   If `RemapUsers` is available, this is the first gid that is
   remapped, and all lower gids are mapped to the equivalent host
   gid. This defaults to 1, so that the host root gid is in the
   container, as this means a lot less file ownership remapping in the
   container image.

* `RemapUidRanges=`

   This specifies a comma-separated list of ranges (like `10000-20000,40000-50000`) of
   available host uids to use to remap container uids in `RemapUsers`. Alternatively
   it can be a username, which means the available subuids of that user will be used.
   If not specified, the default ranges are chosen as the subuids of the `quadlet`
   user.

* `RemapGidRanges=`

   This specifies a comma-separated list of ranges (like `10000-20000,40000-50000`) of
   available host gids to use to remap container gids in `RemapUsers`. Alternatively
   it can be a username, which means the available subgids of that user will be used.
   If not specified, the default ranges are chosen as the subgids of the `quadlet`
   user.

* `KeepId=` (defaults to `no`, only works for user units)

   If this is enabled, then the user uid will be mapped to itself in the container,
   otherwise it is mapped to root. This is ignored for system units.

* `Notify=` (defaults to `no`)

   By default podman is run in such a way that systemd startup notify is handled
   by the container runtime. In other words, the service is deemed started when
   the container runtime exec:s the child in the container. However, if
   the container application supports [sd_notify](https://www.freedesktop.org/software/systemd/man/sd_notify.html)
   then setting `Notify`to true will pass the notification details to the container
   allowing it to notify of startup on its own.

* `SocketActivated=` (defaults to `no`)

   If this is true, then the file descriptors and environment variables for socket activation
   is passed to the container. This is only needed for older versions of podman, since podman
   was [recently made to handle this automatically](https://github.com/containers/podman/pull/11316).

* `Timezone=` (default to `local`)

   The timezone to run the container in.

* `RunInit=` (default to `yes`)

   If enabled (and it is by default), the container will have a
   minimal init process inside the container that forwards signals and
   reaps processes.

* `VolatileTmp=` (default to `yes`)

   If enabled (and it is by default), the container will have a fresh tmpfs mounted on `/tmp`.

* `Volume=`

  Mount a volume in the container. This is equivalent to the podman
  `--volume` option, and generally has the form
  `[[SOURCE-VOLUME|HOST-DIR:]CONTAINER-DIR[:OPTIONS]]`.

  As a special case, if `SOURCE-VOLUME` ends with `.volume`, a podman
  named volume called `systemd-$name` will be used as the source, and
  the generated systemd service will contain a dependency on the
  `$name-volume.service`. Such a volume can be automatically be lazily
  created by using a `$name.volume` quadlet file.

  This key can be listed multiple  times.

* `ExposeHostPort=`

  Exposes a port, or a range of ports (e.g. `50-59`) from the host to the container. Equivalent to the podman `--expose` option.
  This key can be listed multiple  times.

* `PublishPort=`

  Exposes a port, or a range of ports (e.g. `50-59`) from the
  container to the host. Equivalent to the podman `--publish` option.
  The format is similar to the podman options, which is of the form
  `ip:hostPort:containerPort`, `ip::containerPort`,
  `hostPort:containerPort` or `containerPort`, where the number of
  host and container ports must be the same (in case of a range).

  If the IP is set to 0.0.0.0 or not set at all, the port will be
  bound on all IPs on the host.

  Note that not listing a host port means that podman will
  automatically select one, and it may be different for each
  invocation of service. This makes that a less useful option.  The
  allocated port can be found with the `podman port` command.

  This key can be listed multiple  times.

* `PodmanArgs=`

  This key contains a list of arguments passed directly to the end of
  the `podman run` command in the generated file (right before the
  image name in the command line). It can be used to access podman
  features otherwise unsupported by quadlet. Since quadlet is unaware
  of what these options to it can cause unexpected interactions, so
  it is ideally not recommended to use this.

  This key can be listed multiple  times.

* `Label=`

  Set one or more OCI labels on the container. The format is a list of
  `key=value` items, similar to `Environment`.

  This key can be listed multiple  times.

* `Annotation=`

  Set one or more OCI annotations on the container. The format is a list of
  `key=value` items, similar to `Environment`.

  This key can be listed multiple  times.

# Volume files

Volume files are named with a `.volume` extension and contain a
section `[Volume]` describing ta named podman volume. The generated
service is a one-time command that ensures that the volume exists on
the host, creating it if needed.

For a volume file named `$NAME.volume`, the generated podman volume
will be called `systemd-$NAME`, and the generated service file
`$NAME-volume.service`.

Using volume units allows containers to depends on volumes being
automatically pre-created. This is particularly interesting if you
need to specify special options to volume creation, as podman will
otherwise create unknown volumes with the default options.

Supported keys in `Volume` group are:

* `User=`

  The host (numeric) uid, or user name to use as owner for the volume

* `Group=`

  The host (numeric) gid, or group name to use as group for the volume

* `Label=`

  Set one or more OCI labels on the volume. The format is a list of
  `key=value` items, similar to `Environment`.

  This key can be listed multiple  times.
