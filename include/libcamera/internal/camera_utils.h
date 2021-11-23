/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * camera_utils.h - Camera related utilities
 */
#ifndef __LIBCAMERA_INTERNAL_CAMERA_UTILS_H__
#define __LIBCAMERA_INTERNAL_CAMERA_UTILS_H__

#include <string.h>

#include "libcamera/internal/v4l2_device.h"

namespace libcamera {

std::string extractModelFromEntityName(const std::string &entityName);
std::string generateIdForV4L2Device(const V4L2Device *dev,
				    const std::string &model);

} /* namespace libcamera */

#endif /* __LIBCAMERA_INTERNAL_CAMERA_UTILS_H__ */
