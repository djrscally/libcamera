/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * framebuffer.h - Frame buffer handling
 */

#pragma once

#include <assert.h>
#include <limits>
#include <stdint.h>
#include <vector>

#include <libcamera/base/class.h>
#include <libcamera/base/span.h>

#include <libcamera/file_descriptor.h>

namespace libcamera {

class Request;

struct FrameMetadata {
	enum Status {
		FrameSuccess,
		FrameError,
		FrameCancelled,
	};

	struct Plane {
		unsigned int bytesused;
	};

	Status status;
	unsigned int sequence;
	uint64_t timestamp;

	Span<Plane> planes() { return planes_; }
	Span<const Plane> planes() const { return planes_; }

private:
	friend class FrameBuffer;

	std::vector<Plane> planes_;
};

class FrameBuffer final : public Extensible
{
	LIBCAMERA_DECLARE_PRIVATE()

public:
	struct Plane {
		static constexpr unsigned int kInvalidOffset = std::numeric_limits<unsigned int>::max();
		FileDescriptor fd;
		unsigned int offset = kInvalidOffset;
		unsigned int length;
	};

	FrameBuffer(const std::vector<Plane> &planes, unsigned int cookie = 0);

	const std::vector<Plane> &planes() const { return planes_; }
	Request *request() const;
	const FrameMetadata &metadata() const { return metadata_; }

	unsigned int cookie() const { return cookie_; }
	void setCookie(unsigned int cookie) { cookie_ = cookie; }

	void cancel() { metadata_.status = FrameMetadata::FrameCancelled; }

private:
	LIBCAMERA_DISABLE_COPY_AND_MOVE(FrameBuffer)

	friend class V4L2VideoDevice; /* Needed to update metadata_. */

	std::vector<Plane> planes_;

	FrameMetadata metadata_;

	unsigned int cookie_;
};

} /* namespace libcamera */
