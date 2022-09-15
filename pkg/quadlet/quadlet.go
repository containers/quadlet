package quadlet

import (
	"fmt"
	"math"
	"os"
	"regexp"
	"strings"
	"unicode"

	"github.com/containers/quadlet/pkg/systemdparser"
)

const (
	UnitDirAdmin  = "/etc/containers/systemd"
	UnitDirDistro = "/usr/share/containers/systemd"

	UnitGroup       = "Unit"
	InstallGroup    = "Install"
	ServiceGroup    = "Service"
	ContainerGroup  = "Container"
	XContainerGroup = "X-Container"
	VolumeGroup     = "Volume"
	XVolumeGroup    = "X-Volume"

	// TODO: These should be configurable
	QuadletUserName   = "quadlet"
	FallbackUidStart  = 1879048192
	FallbackUidLength = 165536
	FallbackGidStart  = 1879048192
	FallbackGidLength = 165536
)

var validPortRange = regexp.MustCompile(`\d+(-\d+)?(/udp|/tcp)?$`)

var supportedContainerKeys = map[string]bool{
	"ContainerName":   true,
	"Image":           true,
	"Environment":     true,
	"Exec":            true,
	"NoNewPrivileges": true,
	"DropCapability":  true,
	"AddCapability":   true,
	"ReadOnly":        true,
	"RemapUsers":      true,
	"RemapUidStart":   true,
	"RemapGidStart":   true,
	"RemapUidRanges":  true,
	"RemapGidRanges":  true,
	"Notify":          true,
	"SocketActivated": true,
	"ExposeHostPort":  true,
	"PublishPort":     true,
	"KeepId":          true,
	"User":            true,
	"Group":           true,
	"HostUser":        true,
	"HostGroup":       true,
	"Volume":          true,
	"PodmanArgs":      true,
	"Label":           true,
	"Annotation":      true,
	"RunInit":         true,
	"VolatileTmp":     true,
	"Timezone":        true,
}

var supportedVolumeKeys = map[string]bool{
	"User":  true,
	"Group": true,
	"Label": true,
}

func replaceExtension(name string, extension string, extraPrefix string, extraSuffix string) string {
	baseName := name

	dot := strings.LastIndexByte(name, '.')
	if dot > 0 {
		baseName = name[:dot]
	}

	return extraPrefix + baseName + extraSuffix + extension
}

var defaultRemapUids, defaultRemapGids *Ranges

func getDefaultRemapUids() *Ranges {
	if defaultRemapUids == nil {
		defaultRemapUids = lookupHostSubuid(QuadletUserName)
		if defaultRemapUids == nil {
			defaultRemapUids =
				NewRanges(FallbackUidStart, FallbackUidLength)
		}
	}
	return defaultRemapUids
}

func getDefaultRemapGids() *Ranges {
	if defaultRemapGids == nil {
		defaultRemapGids = lookupHostSubgid(QuadletUserName)
		if defaultRemapGids == nil {
			defaultRemapGids =
				NewRanges(FallbackGidStart, FallbackGidLength)
		}
	}
	return defaultRemapUids
}

func isPortRange(port string) bool {
	return validPortRange.MatchString(port)
}

func checkForUnknownKeys(unit *systemdparser.UnitFile, groupName string, supportedKeys map[string]bool) error {
	keys := unit.ListKeys(groupName)
	for _, key := range keys {
		if !supportedKeys[key] {
			return fmt.Errorf("Unsupported key '%s' in group '%s' in %s", key, groupName, unit.Path)
		}
	}
	return nil
}

func lookupRanges(unit *systemdparser.UnitFile, groupName string, key string, nameLookup func(string) *Ranges, defaultValue *Ranges) *Ranges {
	v, ok := unit.Lookup(groupName, key)
	if !ok {
		if defaultValue != nil {
			return defaultValue.Copy()
		} else {
			return NewRangesEmpty()
		}
	}

	if len(v) == 0 {
		return NewRangesEmpty()
	}

	if !unicode.IsDigit(rune(v[0])) {
		if nameLookup != nil {
			r := nameLookup(v)
			if r != nil {
				return r
			}
		}
		return NewRangesEmpty()
	}

	return ParseRanges(v)

}

func splitPorts(ports string) []string {
	parts := make([]string, 0)

	// IP address could have colons in it. For example: "[::]:8080:80/tcp, so we split carefully
	start := 0
	end := 0
	for end < len(ports) {
		if ports[end] == '[' {
			end++
			for end < len(ports) && ports[end] != ']' {
				end++
			}
			if end < len(ports) {
				end++ // Skip ]
			}
		} else if ports[end] == ':' {
			parts = append(parts, ports[start:end])
			end++
			start = end
		} else {
			end++
		}
	}

	parts = append(parts, ports[start:end])
	return parts
}

func addIdMaps(podman *PodmanCmdline, argPrefix string, containerId, hostId, remapStartId uint32, availableHostIds *Ranges) {
	if availableHostIds == nil {
		// Map everything by default
		availableHostIds = NewRangesEmpty()
	}

	// Map the first ids up to remapStartId to the host equivalent
	unmappedIds := NewRanges(0, remapStartId)

	// The rest we want to map to availableHostIds. Note that this
	// overlaps unmappedIds, because below we may remove ranges from
	// unmapped ids and we want to backfill those.
	mappedIds := NewRanges(0, math.MaxUint32)

	//* Always map specified uid to specified host_uid
	podman.addIdMap(argPrefix, containerId, hostId, 1)

	// We no longer want to map this container id as its already mapped
	mappedIds.Remove(containerId, 1)
	unmappedIds.Remove(containerId, 1)

	// But also, we don't want to use the *host* id again, as we can only map it once
	unmappedIds.Remove(hostId, 1)
	availableHostIds.Remove(hostId, 1)

	// Map unmapped ids to equivalent host range, and remove from mappedIds to avoid double-mapping
	for _, r := range unmappedIds.Ranges {
		start := r.Start
		length := r.Length

		podman.addIdMap(argPrefix, start, start, length)
		mappedIds.Remove(start, length)
		availableHostIds.Remove(start, length)
	}

	for c_idx := 0; c_idx < len(mappedIds.Ranges) && len(availableHostIds.Ranges) > 0; c_idx++ {
		c_range := &mappedIds.Ranges[c_idx]
		c_start := c_range.Start
		c_length := c_range.Length

		for c_length > 0 && len(availableHostIds.Ranges) > 0 {
			h_range := &availableHostIds.Ranges[0]
			h_start := h_range.Start
			h_length := h_range.Length

			next_length := minUint32(h_length, c_length)

			podman.addIdMap(argPrefix, c_start, h_start, next_length)
			availableHostIds.Remove(h_start, next_length)
			c_start += next_length
			c_length -= next_length
		}
	}
}

func ConvertContainer(container *systemdparser.UnitFile, isUser bool) (*systemdparser.UnitFile, error) {
	service := container.Dup()
	service.Filename = replaceExtension(container.Filename, ".service", "", "")

	if container.Path != "" {
		service.Add(UnitGroup, "SourcePath", container.Path)
	}

	if err := checkForUnknownKeys(container, ContainerGroup, supportedContainerKeys); err != nil {
		return nil, err
	}

	// Rename old Container group to x-Container so that systemd ignores it
	service.RenameGroup(ContainerGroup, XContainerGroup)

	image, ok := container.Lookup(ContainerGroup, "Image")
	if !ok || len(image) == 0 {
		return nil, fmt.Errorf("No Image key specified")
	}

	containerName, ok := container.Lookup(ContainerGroup, "ContainerName")
	if !ok || len(containerName) == 0 {
		// By default, We want to name the container by the service name
		containerName = "systemd-%N"
	}

	// Set PODMAN_SYSTEMD_UNIT so that podman auto-update can restart the service.
	service.Add(ServiceGroup, "Environment", "PODMAN_SYSTEMD_UNIT=%n")

	// Only allow mixed or control-group, as nothing else works well
	killMode, ok := service.Lookup(ServiceGroup, "KillMode")
	if !ok || !(killMode == "mixed" || killMode == "control-group") {
		if ok {
			return nil, fmt.Errorf("Invalid KillMode '%s'", killMode)
		}

		// We default to mixed instead of control-group, because it lets conmon do its thing
		service.Set(ServiceGroup, "KillMode", "mixed")
	}

	// Read env early so we can override it below
	podmanEnv := container.LookupAllKeyVal(ContainerGroup, "Environment")

	// Need the containers filesystem mounted to start podman
	service.Add(UnitGroup, "RequiresMountsFor", "%t/containers")

	// Remove any leftover cid file before starting, just to be sure.
	// We remove any actual pre-existing container by name with --replace=true.
	// But --cidfile will fail if the target exists.
	service.Add(ServiceGroup, "ExecStartPre", "-rm -f %t/%N.cid")

	// If the conman exited uncleanly it may not have removed the container, so force it,
	// -i makes it ignore non-existing files.
	service.Add(ServiceGroup, "ExecStopPost", "-/usr/bin/podman rm -f -i --cidfile=%t/%N.cid")

	// Remove the cid file, to avoid confusion as the container is no longer running.
	service.Add(ServiceGroup, "ExecStopPost", "-rm -f %t/%N.cid")

	podman := NewPodmanCmdline("run")

	podman.addf("--name=%s", containerName)

	podman.add(
		// We store the container id so we can clean it up in case of failure
		"--cidfile=%t/%N.cid",

		// And replace any previous container with the same name, not fail
		"--replace",

		// On clean shutdown, remove container
		"--rm",

		// Detach from container, we don't need the podman process to hang around
		"-d",

		// But we still want output to the journal, so use the log driver.
		// TODO: Once available we want to use the passthrough log-driver instead.
		"--log-driver", "journald",

		// Never try to pull the image during service start
		"--pull=never")

	// We use crun as the runtime and delegated groups to it
	service.Add(ServiceGroup, "Delegate", "yes")
	podman.add(
		"--runtime", "/usr/bin/crun",
		"--cgroups=split")

	timezone, ok := container.Lookup(ContainerGroup, "Timezone")
	if ok && len(timezone) > 0 {
		podman.addf("--tz=%s", timezone)
	}

	// Run with a pid1 init to reap zombies by default (as most apps don't do that)
	runInit := container.LookupBoolean(ContainerGroup, "RunInit", true)
	if runInit {
		podman.add("--init")
	}

	// By default we handle startup notification with conmon, but allow passing it to the container with Notify=yes
	notify := container.LookupBoolean(ContainerGroup, "Notify", false)
	if notify {
		podman.add("--sdnotify=container")
	} else {
		podman.add("--sdnotify=conmon")
	}
	service.Setv(ServiceGroup,
		"Type", "notify",
		"NotifyAccess", "all")

	if !container.HasKey(ServiceGroup, "SyslogIdentifier") {
		service.Set(ServiceGroup, "SyslogIdentifier", "%N")
	}

	// Default to no higher level privileges or caps
	noNewPrivileges := container.LookupBoolean(ContainerGroup, "NoNewPrivileges", true)
	if noNewPrivileges {
		podman.add("--security-opt=no-new-privileges")
	}

	dropCaps := []string{"all"} // Default
	if container.HasKey(ContainerGroup, "DropCapability") {
		dropCaps = container.LookupAll(ContainerGroup, "DropCapability")
	}

	for _, caps := range dropCaps {
		podman.addf("--cap-drop=%s", strings.ToLower(caps))
	}

	// But allow overrides with AddCapability
	addCaps := container.LookupAll(ContainerGroup, "AddCapability")
	for _, caps := range addCaps {
		podman.addf("--cap-add=%s", strings.ToLower(caps))
	}

	readOnly := container.LookupBoolean(ContainerGroup, "ReadOnly", false)
	if readOnly {
		podman.add("--read-only")
	}

	// We want /tmp to be a tmpfs, like on rhel host
	volatileTmp := container.LookupBoolean(ContainerGroup, "VolatileTmp", true)
	if volatileTmp {
		/* Read only mode already has a tmpfs by default */
		if !readOnly {
			podman.add("--tmpfs", "/tmp:rw,size=512M,mode=1777")
		}
	} else if readOnly {
		/* !volatileTmp, disable the default tmpfs from --read-only */
		podman.add("--read-only-tmpfs=false")
	}

	socketActivated := container.LookupBoolean(ContainerGroup, "SocketActivated", false)
	if socketActivated {
		// TODO: This will not be needed with later podman versions that support activation directly:
		//  https://github.com/containers/podman/pull/11316
		podman.add("--preserve-fds=1")
		podmanEnv["LISTEN_FDS"] = "1"

		// TODO: This will not be 2 when catatonit forwards fds:
		//  https://github.com/openSUSE/catatonit/pull/15
		podmanEnv["LISTEN_PID"] = "2"
	}

	defaultContainerUid := uint32(0)
	defaultContainerGid := uint32(0)

	keepId := container.LookupBoolean(ContainerGroup, "KeepId", false)
	if keepId {
		if isUser {
			defaultContainerUid = uint32(os.Getuid())
			defaultContainerGid = uint32(os.Getgid())
			podman.add("--userns", "keep-id")
		} else {
			return nil, fmt.Errorf("Key 'KeepId' in '%s' unsupported for system units", container.Path)
		}
	}

	uid := container.LookupUint32(ContainerGroup, "User", uint32(defaultContainerUid))
	gid := container.LookupUint32(ContainerGroup, "Group", uint32(defaultContainerGid))

	hostUid, err := container.LookupUid(ContainerGroup, "HostUser", uid)
	if err != nil {
		return nil, fmt.Errorf("Key 'HostUser' invalid: %s", err)
	}

	hostGid, err := container.LookupGid(ContainerGroup, "HostGroup", gid)
	if err != nil {
		return nil, fmt.Errorf("Key 'HostGroup' invalid: %s", err)
	}

	if uid != defaultContainerUid || gid != defaultContainerUid {
		podman.add("--user")
		if gid == defaultContainerGid {
			podman.addf("%d", uid)
		} else {
			podman.addf("%d:%d", uid, gid)
		}
	}

	var remapUsers bool
	if isUser {
		remapUsers = false
	} else {
		remapUsers = container.LookupBoolean(ContainerGroup, "RemapUsers", false)
	}

	if !remapUsers {
		// No remapping of users, although we still need maps if the
		//   main user/group is remapped, even if most ids map one-to-one.
		if uid != hostUid {
			addIdMaps(podman, "--uidmap", uid, hostUid, math.MaxUint32, nil)
		}
		if gid != hostGid {
			addIdMaps(podman, "--gidmap", gid, hostGid, math.MaxUint32, nil)
		}
	} else {
		uid_remap_ids := lookupRanges(container, ContainerGroup, "RemapUidRanges", lookupHostSubuid, getDefaultRemapUids())
		gid_remap_ids := lookupRanges(container, ContainerGroup, "RemapGidRanges", lookupHostSubgid, getDefaultRemapGids())
		remap_uid_start := container.LookupUint32(ContainerGroup, "RemapUidStart", 1)
		remap_gid_start := container.LookupUint32(ContainerGroup, "RemapGidStart", 1)

		addIdMaps(podman, "--uidmap", uid, hostUid, remap_uid_start, uid_remap_ids)
		addIdMaps(podman, "--gidmap", gid, hostGid, remap_gid_start, gid_remap_ids)
	}

	volumes := container.LookupAll(ContainerGroup, "Volume")
	for _, volume := range volumes {
		parts := strings.SplitN(volume, ":", 3)

		source := ""
		dest := ""
		options := ""
		if len(parts) >= 2 {
			source = parts[0]
			dest = parts[1]
		} else {
			dest = parts[0]
		}
		if len(parts) >= 3 {
			options = ":" + parts[2]
		}

		if source != "" {
			if source[0] == '/' {
				// Absolute path
				service.Add(UnitGroup, "RequiresMountsFor", source)
			} else {
				// unit name (with .volume suffix) or named podman volume

				if strings.HasSuffix(source, ".volume") {
					// the podman volume name is systemd-$name
					volume_name := replaceExtension(source, "", "systemd-", "")

					// the systemd unit name is $name-volume.service
					volume_service_name := replaceExtension(source, ".service", "", "-volume")

					source = volume_name

					service.Add(UnitGroup, "Requires", volume_service_name)
					service.Add(UnitGroup, "After", volume_service_name)
				}
			}
		}

		podman.add("-v")
		if source == "" {
			podman.add(dest)
		} else {
			podman.addf("%s:%s%s", source, dest, options)
		}
	}

	exposed_ports := container.LookupAll(ContainerGroup, "ExposeHostPort")
	for _, exposed_port := range exposed_ports {
		exposed_port = strings.TrimSpace(exposed_port) // Allow whitespace after

		if !isPortRange(exposed_port) {
			return nil, fmt.Errorf("Invalid port format '%s'", exposed_port)
		}

		podman.addf("--expose=%s", exposed_port)
	}

	publish_ports := container.LookupAll(ContainerGroup, "PublishPort")
	for _, publish_port := range publish_ports {
		publish_port = strings.TrimSpace(publish_port) // Allow whitespace after

		// IP address could have colons in it. For example: "[::]:8080:80/tcp, so use custom splitter
		parts := splitPorts(publish_port)

		container_port := ""
		ip := ""
		host_port := ""

		// format (from podman run):
		// ip:hostPort:containerPort | ip::containerPort | hostPort:containerPort | containerPort
		//
		// ip could be IPv6 with minimum of these chars "[::]"
		// containerPort can have a suffix of "/tcp" or "/udp"
		//

		switch len(parts) {
		case 1:
			container_port = parts[0]

		case 2:
			host_port = parts[0]
			container_port = parts[1]

		case 3:
			ip = parts[0]
			host_port = parts[1]
			container_port = parts[2]

		default:
			return nil, fmt.Errorf("Invalid published port '%s'", publish_port)
		}

		if ip == "0.0.0.0" {
			ip = ""
		}

		if len(host_port) > 0 && !isPortRange(host_port) {
			return nil, fmt.Errorf("Invalid port format '%s'", host_port)
		}

		if len(container_port) > 0 && !isPortRange(container_port) {
			return nil, fmt.Errorf("Invalid port format '%s'", container_port)
		}

		if len(ip) > 0 {
			if len(host_port) > 0 {
				podman.addf("-p=%s:%s:%s", ip, host_port, container_port)
			} else {
				podman.addf("-p=%s::%s", ip, container_port)
			}
		} else if len(host_port) > 0 {
			podman.addf("-p=%s:%s", host_port, container_port)
		} else {
			podman.addf("-p=%s", container_port)
		}
	}

	podman.addEnv(podmanEnv)

	labels := container.LookupAllKeyVal(ContainerGroup, "Label")
	podman.addLabels(labels)

	annotations := container.LookupAllKeyVal(ContainerGroup, "Annotation")
	podman.addAnnotations(annotations)

	podman_args := container.LookupAllArgs(ContainerGroup, "PodmanArgs")
	podman.add(podman_args...)

	podman.add(image)

	exec_args, ok := container.LookupLastArgs(ContainerGroup, "Exec")
	if ok {
		podman.add(exec_args...)
	}

	service.AddCmdline(ServiceGroup, "ExecStart", podman.Args)

	return service, nil
}

func ConvertVolume(volume *systemdparser.UnitFile, name string) (*systemdparser.UnitFile, error) {
	service := volume.Dup()
	service.Filename = replaceExtension(volume.Filename, ".service", "", "-volume")

	if err := checkForUnknownKeys(volume, VolumeGroup, supportedVolumeKeys); err != nil {
		return nil, err
	}

	/* Rename old Volume group to x-Volume so that systemd ignores it */
	service.RenameGroup(VolumeGroup, XVolumeGroup)

	volume_name := replaceExtension(name, "", "systemd-", "")

	// Need the containers filesystem mounted to start podman
	service.Add(UnitGroup, "RequiresMountsFor", "%t/containers")

	exec_cond := fmt.Sprintf("/usr/bin/bash -c \"! /usr/bin/podman volume exists %s\"", volume_name)

	labels := volume.LookupAllKeyVal(VolumeGroup, "Label")

	podman := NewPodmanCmdline("volume", "create")

	var opts strings.Builder
	opts.WriteString("o=")

	if volume.HasKey(VolumeGroup, "User") {
		uid := volume.LookupUint32(VolumeGroup, "User", 0)
		if opts.Len() > 2 {
			opts.WriteString(",")
		}
		opts.WriteString(fmt.Sprintf("uid=%d", uid))
	}

	if volume.HasKey(VolumeGroup, "Group") {
		gid := volume.LookupUint32(VolumeGroup, "Group", 0)
		if opts.Len() > 2 {
			opts.WriteString(",")
		}
		opts.WriteString(fmt.Sprintf("gid=%d", gid))
	}

	if opts.Len() > 2 {
		podman.add("--opt", opts.String())
	}

	podman.addLabels(labels)
	podman.add(volume_name)

	service.AddCmdline(ServiceGroup, "ExecStart", podman.Args)

	service.Setv(ServiceGroup,
		"Type", "oneshot",
		"RemainAfterExit", "yes",
		"ExecCondition", exec_cond,

		// The default syslog identifier is the exec basename (podman) which isn't very useful here
		"SyslogIdentifier", "%N")

	return service, nil
}
