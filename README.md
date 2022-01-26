# What is Quadlet

Quadlet is an opinionated tool for easily running podman system
containers under systemd in an optimal way.

# Why would I want that

Containers are often used in a cloud context, and they are then used
in combination with an orchestrator like Kubernetes. They are also
commonly used during development and testing to manually manage
containers on an ad-hoc basis.

However, there are also use cases where you want some kind of automatic
container management, but on a smaller, single-node scale, and often
more tightly integrated with the rest of the system. Typical examples
of this can be embedded or automotive use, where there is no system
administrator, or disconnected or EDGE servers.

The recommended way to do this is to use systemd to orchestrate the
containers, since this is an already running process manager, and
since podman containers are just child processes. There are many
documents that describe how to use podman with systemd directly, but
the end result are generally large, hard to maintain systemd config
files. And often the container setup isn't optimal.

With quadlet, you describe how to run a container in a format that is
very similar to regular systemd config files. From these actual
systemd configurations are automatically generated (using [systemd
generators](https://www.freedesktop.org/software/systemd/man/systemd.generator.html)).

The container descriptions focus on the relevant container details,
with no technical details about how the podman integration works. This
means they are easy to write, easy to maintain and integration can
automatically improve over time as new podman features become
available.

# A container example

Here is a minimal container config:

```
[Unit]
Description=A minimal container

[Container]
Image=centos
Exec=sleep 60

[Service]
Restart=always
```

This is very similar to a regular systemd service file, except for the
`[Container]` section. It will run `sleep 60` in a centos container, and
then exit only for systemd to restart it again.

If you put this in `/etc/containers/systemd/minimal.container` and
then run `systemctl daemon-reload` and `podman pull centos` you can
immediately start the container using `systemctl start
minimal.service` and watch the status:

```
# systemctl status minimal.service
● minimal.service - A minimal container
     Loaded: loaded (/etc/containers/systemd/minimal.container; generated)
     Active: active (running) since Thu 2021-09-23 13:05:33 CEST; 1s ago
    Process: 839846 ExecStartPre=rm -f /run/minimal.cid (code=exited, status=0/SUCCESS)
   Main PID: 839894 (conmon)
      Tasks: 4 (limit: 38375)
     Memory: 1.4M
        CPU: 193ms
     CGroup: /system.slice/minimal.service
             ├─container
             │ ├─839898 /dev/init -- sleep 60
             │ └─839943 /usr/bin/coreutils --coreutils-prog-shebang=sleep /usr/bin/sleep 60
             └─supervisor
               └─839894 /usr/bin/conmon ...
```

The generated service file is in
`/run/systemd/generator/minimal.service` for people interested in all
the technical details.

# Building quadlet

Quadlet builds using meson. You can build and install it with these
steps:

```
$ meson builddir  --prefix /usr
$ cd builddir
$ meson compile
$ meson install
```

This will install quadlet-generator in `/usr/lib/systemd/system-generators`, which will
read configuration files from `/etc/containers/systemd`.

# Where to go from here

Here are some further documentations:

 * [File formats](./docs/Fileformat.md)
 * [Container setup](./docs/ContainerSetup.md)

Quadlet also ships with some [example containers](./examples).
