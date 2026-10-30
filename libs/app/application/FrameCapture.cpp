#include "application/FrameCapture.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace app {

	namespace {
		Distribution distribution(std::vector<double> values) {
			if (values.empty()) {
				return {};
			}
			std::ranges::sort(values);
			const auto rank = [&values](double quantile) {
				const auto index = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(values.size())));
				return values[std::clamp<std::size_t>(index, 1, values.size()) - 1];
			};
			return {
				.p50 = rank(0.5),
				.p99 = rank(0.99),
				.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size()),
				.max = values.back(),
			};
		}

		template <typename Field>
		Distribution over(const std::vector<FrameSample>& samples, Field field) {
			std::vector<double> values;
			values.reserve(samples.size());
			for (const FrameSample& sample : samples) {
				values.push_back(static_cast<double>(sample.*field));
			}
			return distribution(std::move(values));
		}

		nlohmann::ordered_json toJson(const Distribution& d) {
			return {{"p50", d.p50}, {"p99", d.p99}, {"mean", d.mean}, {"max", d.max}};
		}
	} // namespace

	nlohmann::ordered_json CaptureSummary::toJson() const {
		return {
			{"frames", frames},
			{"droppedFrames", droppedFrames},
			{"seconds", seconds},
			{"frameMs", app::toJson(frameMs)},
			{"cpuMs", app::toJson(cpuMs)},
			{"presentMs", app::toJson(presentMs)},
			{"renderMs", app::toJson(renderMs)},
			{"flushMs", app::toJson(flushMs)},
			{"gpuFrames", gpuFrames},
			{"gpuMs", app::toJson(gpuMs)},
			{"drawCalls", app::toJson(drawCalls)},
			{"vertices", app::toJson(vertices)},
			{"triangles", app::toJson(triangles)},
			{"instances", app::toJson(instances)},
			{"uploadBytes", app::toJson(uploadBytes)},
		};
	}

	void FrameCapture::start() {
		samples.clear();
		samples.reserve(kMaxFrames);
		gpuSamples.clear();
		gpuSamples.reserve(kMaxFrames);
		dropped = 0;
		running = true;
	}

	void FrameCapture::add(const FrameSample& sample) {
		if (samples.size() == kMaxFrames) {
			++dropped;
			return;
		}
		samples.push_back(sample);
	}

	void FrameCapture::addGpu(float ms) {
		if (gpuSamples.size() < kMaxFrames) {
			gpuSamples.push_back(ms);
		}
	}

	CaptureSummary FrameCapture::stop() {
		running = false;
		CaptureSummary summary{
			.frames = samples.size(),
			.droppedFrames = dropped,
			.frameMs = over(samples, &FrameSample::frameMs),
			.cpuMs = over(samples, &FrameSample::cpuMs),
			.presentMs = over(samples, &FrameSample::presentMs),
			.renderMs = over(samples, &FrameSample::renderMs),
			.flushMs = over(samples, &FrameSample::flushMs),
			.gpuFrames = gpuSamples.size(),
			.gpuMs = distribution({gpuSamples.begin(), gpuSamples.end()}),
			.drawCalls = over(samples, &FrameSample::drawCalls),
			.vertices = over(samples, &FrameSample::vertices),
			.triangles = over(samples, &FrameSample::triangles),
			.instances = over(samples, &FrameSample::instances),
			.uploadBytes = over(samples, &FrameSample::uploadBytes),
		};
		for (const FrameSample& sample : samples) {
			summary.seconds += static_cast<double>(sample.frameMs) / 1000.0;
		}
		return summary;
	}

} // namespace app
