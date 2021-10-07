# Container setup

Quadlet tries to run containers in a way that efficiently interacts
with systemd and the rest of the host. It also tries to set up the
container in a way that defaults to being safe, and as similar to a
host system as possible. This makes sense, because services running
under quadlet (as opposed to to a cloud orchestrator) is likely to be
system services, rather than completely isolated network services.

# CGroup setup

The goal is to have systemd be fully in control of all the processes
in the container. This means that the cgroups of the container must
be beneath the cgroup that systemd creates. This is achieved by using
the crun container runtime with the split cgroups feature.

With a traditional podman setup the cgroup hieararchy looks like this:

```
├─system.slice
│ └─the-app.service …
│   └─2129004 /usr/bin/conmon --api-version 1 -c ce7fc6971541930d7f00c7436d90ab…
└─machine.slice
  └─libpod-ce7fc6971541930d7f00c7436d90ab71583754fae0d5e81063ac7e405be158f8.scope …
    └─container
      └─2129005 /usr/bin/the-app
```

Whereas with the split cgroup it looks like this:

```
└─system.slice
  └─the-app.service …
    ├─supervisor
    │ └─2129004 /usr/bin/conmon --api-version 1 -c 8b655cd6d3761b6b0929b6c37906…
    └─container
      └─2129005 /usr/bin/the-app
```

Since all the processes are under the service cgroup systemd is aware
of everything that is part of the container. It also means the systemd
cgroup options affect the container.

Using crun also has some other advantages, as it is lighter and higher
performance than runc.

# Robust container shutdown

One issue with using systemd to manage the service is that there is a
risk for container shutdowns to become unclean, leaving around
leftover container state. Podman doesn't have a centralized daemon
like docker that keeps track of what is running. However, there is
still global state that can be accessed with things like `podman ps`
or `podman inspect`. This global state is managed by the `conman`
process that monitors each container. It tracks the container
subprocess and when it dies it runs some cleanup code in an atexit()
handler that update the global state wrt to the container not living.

In the normal case this is not a problem, but if systemd has to force
kill everything in the cgroup this can lead to problems, like multiple
versions of the container seemingly running at the same time (and
therefore not letting the new container have the same name as the old
one).

This is solved by generating files like this:

```
ExecStartPre=-rm -f %t/%N.cid
ExecStart=/usr/bin/podman run \
         --name=systemd-%N --replace=true \
         --cidfile=%t/%N.cid \
          --rm -d  --sdnotify=conmon \
         image
ExecStopPost=-/usr/bin/podman rm -f -i --cidfile=%t/%N.cid
ExecStopPost=-rm -f %t/%N.cid
KillMode=mixed # or control-group
Type=notify
NotifyAccess=all
```

`--name=systemd-%N --replace=true` means if the container with the
name already exists (due to possible unclean shutdown) it will be
removed before starting the new one. We also run a podman rm in
ExecStopPost to make sure that the podman global state is correct
until we restart. In theory only the post-stop removal should be
needed, but it doesn't hurt to specify both.

`--rm` means the container state is automatically removed once the
container exits. We want this because the container lifetime is
supposed to be that of the running service.

`-d` means the launching `podman` process exits immediately rather
than (unnecessarily) staying around for the lifetime of the
service. Rather than waiting on the initial podmon proces quadlet
instead uses `--sdnotify=conmon` and `Type=notify` which means that
reporting of when the process is ready, and reporting the exit code
from the main process is handled by conmon. Additionally, if the
container itself supports notification, and specifies `Notify=true`,
then we instead pass `--sdnotify=container`.

We default to `Killmode=mixed` which means a controlled shutdown will
send SIGTERM to the main process in the service. This is the conmon
process, but that will be forwarded to the main process in the
container. If the container exits cleanly within `TimeoutStopSec`
seconds then everything is great, but if not, *all* the processes in
the service (i.e. conmon + the container) will get sent a hard SIGKILL
and die.

If this happens we will not run the atexit() handler in conmon which
will cause problems the next time the service starts. To avoid this
problem we use `--cidfile` to record the container id of the container
in `/run/servicename.cid`. If this file exists when starting a
container we clean up the old container state and remove the file.

It is also possible to specify `KillMode=control-group` in the
container file (no other options are supported), which will behave
similarly, except the initial SIGTERM will be sent to *all* the
processes in the container, not just the main one.

# Logging

With podman the container stdout is a pipe set up by conmon which
collects all the logged info and redirects its to the global podman
logs (available via `podman logs`). It is also possible to use `podman
attach` or `podman run -ti` to get at the output. This contacts the
conman process and asks to get copied on the output.

For systemd services we are primarily interested with getting the logs
into the systemd journal. For this quadlet uses `--log-driver journal`
which causes conmon to send the logs to the journal.

Unfortunately this is still not ideal, as it causes an extra copy of
the log output from the container to conmon, as well as losing
information about exactly which pid sent the message. Long term we
would like to connect stdout from the container directly to the
journal, via the new `--log-driver passthrough` option that is [in
development](https://github.com/containers/podman/pull/11390).


# Standardized container environment

We expect system containers to be more like linux system code than
typical web-server containers, so we want to ensure that the runtime
environment in the container is similar to that of a normal systemd
service. We also want quadlet containers look the same when viewed
from the podman side.

Here are some things that are set up:

* `--name=systemd-%N`

  Set the name of the podman container (in e.g. podman ps output) to be systemd-servicename.

* `SyslogIdentifier=%N`

  The default syslog identifier set up by systemd is derived from the
  main binary name, which is always "podman" for containers, so all the
  units have `SyslogIdentifier=%N` to instead set this to the unit
  name.

* `--init`

  This adds a minimal pid1 babysitter process for the container that
  reaps zombied children. This is necessary because most programs are
  not programmed to reap the child processes they launch, instead
  relying on pid 1 to do this. But when run as pid 1 of in container
  there is no other reaper around. This can be overridden with
  `RunInit=no`.

* `--mount type=tmpfs,tmpfs-size=512M,destination=/tmp`

  This makes /tmp in the container be a tmpfs, similar to how it is set
  up on the host. This can be overridden with `VolatileTmp=no`.

* `--tz=local`

  This sets the timezone of the container to match whatever the host os
  uses. For distributed host-isolated services it makes sense to always
  run in UTC, but for a system service we want to be as close as
  possible to the host. This can be overridden with `Timnezone=`.

* `--pull=never`

  Never pull the image during service start. If the image is missing this
  is a bug and we don't want to do unexpected network access in the systemd
  unit.

* `--cap-drop=all`

  Most apps need no special capabilities, so default to none unless
  specifically needed. If some special capability is needed it you
  can add thes using e.g. `AddCapability=CAP_DAC_OVERRIDE`.
  This can be overridden with `DropCapability=`.


* `--security-opt=no-new-privileges`

  Generally services run only as one user and need not special permissions.
  This disables all forms of setuid like features that allows the process to
  gain privileges it didn't initially have. Unless the app has very specific
  needs this is a good default for security reasons.
  This can be overridden with `NoNewPrivileges=no`.

# Uid/Gid mapping

System serices typically run as a single user, ideally not root. For
containers the situation is a bit more complex as thee uids inside the
container can be different than the ones on the host. This mapping has
two sides, first of all the kernel user namespaces can map the
(dynamic) uids for the running processes so that they are different
depending on the context. The other side is the (static) ownership of
the container image files on disk. This is handled by podman creating
an extra layer on disk for each specific mapping.

The user namespace mapping is essentially free, but the file ownership
mapping takes both time and diskspace. For this reason, and because
most container files are owned by root, quadlet defaults to mapping
host uid 0 to 0 in the container, making the ownership mapping layers
small. Due to the limited permissions quadlet defaults to, mapping the
root user into the container should be safe. The default can be
overridden with `RemapUsers=no` which will map the host uid/gids
directly to the container.

Due to the above 0-to-0 mapping, the recommended approach is to
construct container images that run as a standardized *non-root* user,
and specify that user with `User=that_uid`. If needed you can then you
can allocate the real uid dynamic on the host and specify it using
`HostUser`. It is possible to specify a root User, but this will cause
a larger remapping layer so it is not recommended. If a particular
permission is required, instead use `AddCapabiltity`.

All the above also applies to group ids.
