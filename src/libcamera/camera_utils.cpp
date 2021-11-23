/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * camera_utils.cpp - Camera related utilities
 */

#include "libcamera/internal/camera_utils.h"

#include <regex>

#include "libcamera/internal/sysfs.h"

/**
 * \file camera_utils.h
 * \brief Utilities for Camera instances
 */

namespace libcamera {

/**
 * \brief Extract the sensor or lens model name from the media entity name
 * \param[in] entityName The entity name of a lens or sensor
 *
 * \return Model name as string
 */
std::string extractModelFromEntityName(const std::string &entityName)
{
	/*
	 * Extract the sensor or lens model name from the media entity name.
	 *
	 * There is no standardized naming scheme for sensor entities in the
	 * Linux kernel at the moment.
	 *
	 * - The most common rule, used by I2C sensors, associates the model
	 *   name with the I2C bus number and address (e.g. 'imx219 0-0010').
	 *
	 * - When the sensor exposes multiple subdevs, the model name is
	 *   usually followed by a function name, as in the smiapp driver (e.g.
	 *   'jt8ew9 pixel_array 0-0010').
	 *
	 * - The vimc driver names its sensors 'Sensor A' and 'Sensor B'.
	 *
	 * Other schemes probably exist. As a best effort heuristic, use the
	 * part of the entity name before the first space if the name contains
	 * an I2C address, and use the full entity name otherwise.
	 */
	std::regex i2cRegex{ " [0-9]+-[0-9a-f]{4}" };
	std::smatch match;

	std::string model;
	if (std::regex_search(entityName, match, i2cRegex))
		model = entityName.substr(0, entityName.find(' '));
	else
		model = entityName;

	return model;
}

/**
 * \brief Generate ID for V4L2 device
 * \param[in] dev The V4L2Device
 * \param[in] model The ModelName
 *
 * Contruct ID from the firmware description. If it doesn't exist, contruct it
 * from the device path and the provided model name.
 * If both fails, return an empty string.
 *
 * \return ID as string
 */
std::string generateIdForV4L2Device(const V4L2Device *dev,
				    const std::string &model)
{
	const std::string devPath = dev->devicePath();

	/* Try to get ID from firmware description. */
	std::string id = sysfs::firmwareNodePath(devPath);
	if (!id.empty())
		return id;

	/*
	 * Virtual device not described in firmware
	 *
	 * Verify it's a platform device and construct ID from the deive path
	 * and model of a sensor or lens.
	 */
	if (devPath.find("/sys/devices/platform/", 0) == 0) {
		return devPath.substr(strlen("/sys/devices/")) + " " + model;
	}

	return "";
}

} /* namespace libcamera */
