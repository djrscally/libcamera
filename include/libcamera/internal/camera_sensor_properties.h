/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * camera_sensor_properties.h - Database of camera sensor properties
 */

#pragma once

#include <map>
#include <string>

#include <libcamera/geometry.h>

namespace libcamera {

struct CameraSensorProperties {
	static const CameraSensorProperties *get(const std::string &sensor);

	Size unitCellSize;
	std::map<int32_t, int32_t> testPatternModes;
};

} /* namespace libcamera */
