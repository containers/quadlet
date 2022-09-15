package quadlet

import (
	"fmt"
	"sort"
)

/* This is a helper for constructing podman commandlines */
type PodmanCmdline struct {
	Args []string
}

func (c *PodmanCmdline) add(args ...string) {
	c.Args = append(c.Args, args...)
}

func (c *PodmanCmdline) addf(format string, a ...any) {
	c.add(fmt.Sprintf(format, a...))
}

func (c *PodmanCmdline) addKeys(arg string, keys map[string]string) {
	ks := make([]string, 0, len(keys))
	for k := range keys {
		ks = append(ks, k)
	}
	sort.Strings(ks)

	for _, k := range ks {
		c.add(arg, fmt.Sprintf("%s=%s", k, keys[k]))
	}
}

func (c *PodmanCmdline) addEnv(env map[string]string) {
	c.addKeys("--env", env)
}

func (c *PodmanCmdline) addLabels(labels map[string]string) {
	c.addKeys("--label", labels)
}

func (c *PodmanCmdline) addAnnotations(annotations map[string]string) {
	c.addKeys("--annotation", annotations)
}

func (c *PodmanCmdline) addIdMap(argPrefix string, containerIdStart, hostIdStart, numIds uint32) {
	if numIds != 0 {
		c.add(argPrefix)
		c.addf("%d:%d:%d", containerIdStart, hostIdStart, numIds)
	}
}

func NewPodmanCmdline(args ...string) *PodmanCmdline {
	c := &PodmanCmdline{
		Args: make([]string, 0),
	}

	c.add("/usr/bin/podman")
	c.add(args...)
	return c
}
