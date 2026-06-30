#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#define SMEXT_CONF_NAME "BSP-Peek"
#define SMEXT_CONF_DESCRIPTION \
	"\"Peek\" into BSP engine and file data (brushes, leaves, planes, nodes, " \
	"displacements, props)"
#define SMEXT_CONF_VERSION    "1.6.0"
#define SMEXT_CONF_AUTHOR     "jvnipers"
#define SMEXT_CONF_URL        "https://github.com/jvnipers/bsp-peek"
#define SMEXT_CONF_LOGTAG     "BSPP"
#define SMEXT_CONF_LICENSE    "AGPLv3"
#define SMEXT_CONF_DATESTRING __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#define SMEXT_ENABLE_FORWARDSYS
#define SMEXT_ENABLE_HANDLESYS
#define SMEXT_ENABLE_PLAYERHELPERS
#define SMEXT_ENABLE_GAMECONF
#define SMEXT_ENABLE_GAMEHELPERS

#endif
