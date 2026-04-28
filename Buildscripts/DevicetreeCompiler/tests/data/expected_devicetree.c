// Default headers
#include <tactility/device.h>
#include <tactility/dts.h>
#include <tactility/module.h>
// DTS headers
#include <test_include.h>

static const root_config_dt root_config = {
	"Test Model"
};

static struct Device root = {
	.name = "/",
	.config = &root_config,
	.parent = NULL,
	.internal = NULL
};

static const generic_device_config_dt test_device@0_config = {
	0,
	42,
	"hello"
};

static struct Device test_device@0 = {
	.name = "test-device@0",
	.config = &test_device@0_config,
	.parent = &root,
	.internal = NULL
};

static const bool_device_config_dt bool_test_device_config = {
	true,
	false,
	true,
	true,
	true
};

static struct Device bool_test_device = {
	.name = "bool-test-device",
	.config = &bool_test_device_config,
	.parent = &root,
	.internal = NULL
};

struct DtsDevice dts_devices[] = {
	{ &root, "test,root", DTS_DEVICE_STATUS_OKAY },
	{ &test_device@0, "test,generic-device", DTS_DEVICE_STATUS_OKAY },
	{ &bool_test_device, "test,bool-device", DTS_DEVICE_STATUS_OKAY },
	DTS_DEVICE_TERMINATOR
};


struct Module* dts_modules[] = {
	NULL
};
