/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * camera_sensor.h - A camera sensor
 */
#ifndef __LIBCAMERA_INTERNAL_CAMERA_SENSOR_H__
#define __LIBCAMERA_INTERNAL_CAMERA_SENSOR_H__

#include <memory>
#include <string>
#include <vector>

#include <libcamera/base/class.h>
#include <libcamera/base/log.h>

#include <libcamera/controls.h>
#include <libcamera/geometry.h>
#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/v4l2_subdevice.h"

namespace libcamera {

class BayerFormat;
class MediaEntity;

class CameraSensor : protected Loggable
{
public:
	explicit CameraSensor(const MediaEntity *entity);
	~CameraSensor();

	int init();

	const std::string &model() const { return model_; }
	const std::string &id() const { return id_; }
	const MediaEntity *entity() const { return entity_; }
	const std::vector<unsigned int> &mbusCodes() const { return mbusCodes_; }
	const std::vector<Size> sizes(unsigned int mbusCode) const;
	Size resolution() const;
	const std::vector<int32_t> &testPatternModes() const
	{
		return testPatternModes_;
	}

	V4L2SubdeviceFormat getFormat(const std::vector<unsigned int> &mbusCodes,
				      const Size &size) const;
	int setFormat(V4L2SubdeviceFormat *format);

	const ControlInfoMap &controls() const;
	ControlList getControls(const std::vector<uint32_t> &ids);
	int setControls(ControlList *ctrls);

	V4L2Subdevice *device() { return subdev_.get(); }

	const ControlList &properties() const { return properties_; }
	int sensorInfo(IPACameraSensorInfo *info) const;

	void updateControlInfo();

protected:
	std::string logPrefix() const override;

private:
	LIBCAMERA_DISABLE_COPY(CameraSensor)

	int validateSensorDriver();
	void initVimcDefaultProperties();
	void initStaticProperties();
	void initTestPatternModes(
		const std::map<int32_t, int32_t> &testPatternModeMap);
	int initProperties();

	const MediaEntity *entity_;
	std::unique_ptr<V4L2Subdevice> subdev_;
	unsigned int pad_;

	std::string model_;
	std::string id_;

	V4L2Subdevice::Formats formats_;
	std::vector<unsigned int> mbusCodes_;
	std::vector<Size> sizes_;
	std::vector<int32_t> testPatternModes_;

	Size pixelArraySize_;
	Rectangle activeArea_;
	const BayerFormat *bayerFormat_;

	ControlList properties_;
};

} /* namespace libcamera */

#endif /* __LIBCAMERA_INTERNAL_CAMERA_SENSOR_H__ */
