/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Ideas On Board
 *
 * ipu3_agc.cpp - AGC/AEC mean-based control algorithm
 */

#include "agc.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <libcamera/base/log.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "libipa/histogram.h"

/**
 * \file agc.h
 */

namespace libcamera {

using namespace std::literals::chrono_literals;

namespace ipa::ipu3::algorithms {

/**
 * \class Agc
 * \brief A mean-based auto-exposure algorithm
 *
 * This algorithm calculates a shutter time and an analogue gain so that the
 * average value of the green channel of the brightest 2% of pixels approaches
 * 0.5. The AWB gains are not used here, and all cells in the grid have the same
 * weight, like an average-metering case. In this metering mode, the camera uses
 * light information from the entire scene and creates an average for the final
 * exposure setting, giving no weighting to any particular portion of the
 * metered area.
 *
 * Reference: Battiato, Messina & Castorina. (2008). Exposure
 * Correction for Imaging Devices: An Overview. 10.1201/9781420054538.ch12.
 */

LOG_DEFINE_CATEGORY(IPU3Agc)

/* Limits for analogue gain values */
static constexpr double kMinAnalogueGain = 1.0;
static constexpr double kMaxAnalogueGain = 8.0;

/* \todo Honour the FrameDurationLimits control instead of hardcoding a limit */
static constexpr utils::Duration kMaxShutterSpeed = 60ms;

/* Histogram constants */
static constexpr uint32_t knumHistogramBins = 256;

/* Target value to reach for the top 2% of the histogram */
static constexpr double kEvGainTarget = 0.5;

/* Number of frames to wait before calculating stats on minimum exposure */
static constexpr uint32_t kNumStartupFrames = 10;

/*
 * Relative luminance target.
 *
 * It's a number that's chosen so that, when the camera points at a grey
 * target, the resulting image brightness is considered right.
 */
static constexpr double kRelativeLuminanceTarget = 0.16;

Agc::Agc()
	: frameCount_(0), lineDuration_(0s), minShutterSpeed_(0s),
	  maxShutterSpeed_(0s), filteredExposure_(0s), currentExposure_(0s)
{
}

/**
 * \brief Configure the AGC given a configInfo
 * \param[in] context The shared IPA context
 * \param[in] configInfo The IPA configuration data
 *
 * \return 0
 */
int Agc::configure(IPAContext &context, const IPAConfigInfo &configInfo)
{
	stride_ = context.configuration.grid.stride;

	/* \todo use the IPAContext to provide the limits */
	lineDuration_ = configInfo.sensorInfo.lineLength * 1.0s
		      / configInfo.sensorInfo.pixelRate;

	minShutterSpeed_ = context.configuration.agc.minShutterSpeed;
	maxShutterSpeed_ = std::min(context.configuration.agc.maxShutterSpeed,
				    kMaxShutterSpeed);

	minAnalogueGain_ = std::max(context.configuration.agc.minAnalogueGain, kMinAnalogueGain);
	maxAnalogueGain_ = std::min(context.configuration.agc.maxAnalogueGain, kMaxAnalogueGain);

	/* Configure the default exposure and gain. */
	context.frameContext.agc.gain = minAnalogueGain_;
	context.frameContext.agc.exposure = minShutterSpeed_ / lineDuration_;

	return 0;
}

/**
 * \brief Estimate the mean value of the top 2% of the histogram
 * \param[in] stats The statistics computed by the ImgU
 * \param[in] grid The grid used to store the statistics in the IPU3
 * \return The mean value of the top 2% of the histogram
 */
double Agc::measureBrightness(const ipu3_uapi_stats_3a *stats,
			      const ipu3_uapi_grid_config &grid) const
{
	/* Initialise the histogram array */
	uint32_t hist[knumHistogramBins] = { 0 };

	for (unsigned int cellY = 0; cellY < grid.height; cellY++) {
		for (unsigned int cellX = 0; cellX < grid.width; cellX++) {
			uint32_t cellPosition = cellY * stride_ + cellX;

			const ipu3_uapi_awb_set_item *cell =
				reinterpret_cast<const ipu3_uapi_awb_set_item *>(
					&stats->awb_raw_buffer.meta_data[cellPosition]
				);

			uint8_t gr = cell->Gr_avg;
			uint8_t gb = cell->Gb_avg;
			/*
			 * Store the average green value to estimate the
			 * brightness. Even the overexposed pixels are
			 * taken into account.
			 */
			hist[(gr + gb) / 2]++;
		}
	}

	/* Estimate the quantile mean of the top 2% of the histogram. */
	return Histogram(Span<uint32_t>(hist)).interQuantileMean(0.98, 1.0);
}

/**
 * \brief Apply a filter on the exposure value to limit the speed of changes
 */
void Agc::filterExposure()
{
	double speed = 0.2;

	/* Adapt instantly if we are in startup phase */
	if (frameCount_ < kNumStartupFrames)
		speed = 1.0;

	if (filteredExposure_ == 0s) {
		/* DG stands for digital gain.*/
		filteredExposure_ = currentExposure_;
	} else {
		/*
		 * If we are close to the desired result, go faster to avoid making
		 * multiple micro-adjustments.
		 * \todo Make this customisable?
		 */
		if (filteredExposure_ < 1.2 * currentExposure_ &&
		    filteredExposure_ > 0.8 * currentExposure_)
			speed = sqrt(speed);

		filteredExposure_ = speed * currentExposure_ +
				filteredExposure_ * (1.0 - speed);
	}

	LOG(IPU3Agc, Debug) << "After filtering, total_exposure " << filteredExposure_;
}

/**
 * \brief Estimate the new exposure and gain values
 * \param[inout] frameContext The shared IPA frame Context
 * \param[in] yGain The gain calculated based on the relative luminance target
 * \param[in] iqMeanGain The gain calculated based on the relative luminance target
 */
void Agc::computeExposure(IPAFrameContext &frameContext, double yGain,
			  double iqMeanGain)
{
	/* Get the effective exposure and gain applied on the sensor. */
	uint32_t exposure = frameContext.sensor.exposure;
	double analogueGain = frameContext.sensor.gain;

	/* Use the highest of the two gain estimates. */
	double evGain = std::max(yGain, iqMeanGain);

	/* Consider within 1% of the target as correctly exposed */
	if (std::abs(evGain - 1.0) < 0.01)
		LOG(IPU3Agc, Debug) << "We are well exposed (evGain = "
				    << evGain << ")";

	/* extracted from Rpi::Agc::computeTargetExposure */

	/* Calculate the shutter time in seconds */
	utils::Duration currentShutter = exposure * lineDuration_;

	/*
	 * Update the exposure value for the next computation using the values
	 * of exposure and gain really used by the sensor.
	 */
	utils::Duration effectiveExposureValue = currentShutter * analogueGain;

	LOG(IPU3Agc, Debug) << "Actual total exposure " << currentShutter * analogueGain
			    << " Shutter speed " << currentShutter
			    << " Gain " << analogueGain
			    << " Needed ev gain " << evGain;

	/*
	 * Calculate the current exposure value for the scene as the latest
	 * exposure value applied multiplied by the new estimated gain.
	 */
	currentExposure_ = effectiveExposureValue * evGain;

	/* Clamp the exposure value to the min and max authorized */
	utils::Duration maxTotalExposure = maxShutterSpeed_ * maxAnalogueGain_;
	currentExposure_ = std::min(currentExposure_, maxTotalExposure);
	LOG(IPU3Agc, Debug) << "Target total exposure " << currentExposure_
			    << ", maximum is " << maxTotalExposure;

	/* \todo: estimate if we need to desaturate */
	filterExposure();

	/* Divide the exposure value as new exposure and gain values */
	utils::Duration exposureValue = filteredExposure_;
	utils::Duration shutterTime;

	/*
	* Push the shutter time up to the maximum first, and only then
	* increase the gain.
	*/
	shutterTime = std::clamp<utils::Duration>(exposureValue / minAnalogueGain_,
						  minShutterSpeed_, maxShutterSpeed_);
	double stepGain = std::clamp(exposureValue / shutterTime,
				     minAnalogueGain_, maxAnalogueGain_);
	LOG(IPU3Agc, Debug) << "Divided up shutter and gain are "
			    << shutterTime << " and "
			    << stepGain;

	/* Update the estimated exposure and gain. */
	frameContext.agc.exposure = shutterTime / lineDuration_;
	frameContext.agc.gain = stepGain;
}

/**
 * \brief Estimate the relative luminance of the frame with a given gain
 * \param[in] frameContext The shared IPA frame context
 * \param[in] grid The grid used to store the statistics in the IPU3
 * \param[in] stats The IPU3 statistics and ISP results
 * \param[in] gain The gain to apply to the frame
 * \return The relative luminance
 *
 * This function estimates the average relative luminance of the frame that
 * would be output by the sensor if an additional \a gain was applied.
 *
 * The estimation is based on the AWB statistics for the current frame. Red,
 * green and blue averages for all cells are first multiplied by the gain, and
 * then saturated to approximate the sensor behaviour at high brightness
 * values. The approximation is quite rough, as it doesn't take into account
 * non-linearities when approaching saturation.
 *
 * The relative luminance (Y) is computed from the linear RGB components using
 * the Rec. 601 formula. The values are normalized to the [0.0, 1.0] range,
 * where 1.0 corresponds to a theoretical perfect reflector of 100% reference
 * white.
 *
 * More detailed information can be found in:
 * https://en.wikipedia.org/wiki/Relative_luminance
 */
double Agc::estimateLuminance(IPAFrameContext &frameContext,
			      const ipu3_uapi_grid_config &grid,
			      const ipu3_uapi_stats_3a *stats,
			      double gain)
{
	double redSum = 0, greenSum = 0, blueSum = 0;

	/* Sum the per-channel averages, saturated to 255. */
	for (unsigned int cellY = 0; cellY < grid.height; cellY++) {
		for (unsigned int cellX = 0; cellX < grid.width; cellX++) {
			uint32_t cellPosition = cellY * stride_ + cellX;

			const ipu3_uapi_awb_set_item *cell =
				reinterpret_cast<const ipu3_uapi_awb_set_item *>(
					&stats->awb_raw_buffer.meta_data[cellPosition]
				);
			const uint8_t G_avg = (cell->Gr_avg + cell->Gb_avg) / 2;

			redSum += std::min(cell->R_avg * gain, 255.0);
			greenSum += std::min(G_avg * gain, 255.0);
			blueSum += std::min(cell->B_avg * gain, 255.0);
		}
	}

	/*
	 * Apply the AWB gains to approximate colours correctly, use the Rec.
	 * 601 formula to calculate the relative luminance, and normalize it.
	 */
	double ySum = redSum * frameContext.awb.gains.red * 0.299
		    + greenSum * frameContext.awb.gains.green * 0.587
		    + blueSum * frameContext.awb.gains.blue * 0.114;

	return ySum / (grid.height * grid.width) / 255;
}

/**
 * \brief Process IPU3 statistics, and run AGC operations
 * \param[in] context The shared IPA context
 * \param[in] stats The IPU3 statistics and ISP results
 *
 * Identify the current image brightness, and use that to estimate the optimal
 * new exposure and gain for the scene.
 */
void Agc::process(IPAContext &context, const ipu3_uapi_stats_3a *stats)
{
	/*
	 * Estimate the gain needed to have the proportion of pixels in a given
	 * desired range. iqMean is the mean value of the top 2% of the
	 * cumulative histogram, and we want it to be as close as possible to a
	 * configured target.
	 */
	double iqMean = measureBrightness(stats, context.configuration.grid.bdsGrid);
	double iqMeanGain = kEvGainTarget * knumHistogramBins / iqMean;

	/*
	 * Estimate the gain needed to achieve a relative luminance target. To
	 * account for non-linearity caused by saturation, the value needs to be
	 * estimated in an iterative process, as multiplying by a gain will not
	 * increase the relative luminance by the same factor if some image
	 * regions are saturated.
	 */
	double yGain = 1.0;
	double yTarget = kRelativeLuminanceTarget;

	for (unsigned int i = 0; i < 8; i++) {
		double yValue = estimateLuminance(context.frameContext,
						  context.configuration.grid.bdsGrid,
						  stats, yGain);
		double extraGain = std::min(10.0, yTarget / (yValue + .001));

		yGain *= extraGain;
		LOG(IPU3Agc, Debug) << "Y value: " << yValue
				    << ", Y target: " << yTarget
				    << ", gives gain " << yGain;
		if (extraGain < 1.01)
			break;
	}

	computeExposure(context.frameContext, yGain, iqMeanGain);
	frameCount_++;
}

} /* namespace ipa::ipu3::algorithms */

} /* namespace libcamera */
