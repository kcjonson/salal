#include "ui/shapes/Line.h"

#include "ui/detail/Snapshot.h"

#include <primitives/Primitives.h>

#include <array>

namespace ui {

	namespace {
		constexpr Foundation::Color kStroke = Foundation::Color::white();
	} // namespace

	Line::Line(const Args& args)
		: initialStart(args.start),
		  initialEnd(args.end),
		  lineWidth(args.lineWidth),
		  roundCaps(args.roundCaps) {}

	// The common attributes are in by now, so the box the endpoints make replaces the Args' position
	// and size.
	void Line::onMount() {
		setEndpoints(initialStart, initialEnd);
	}

	void Line::setEndpoints(Foundation::Vec2 start, Foundation::Vec2 end) {
		const float			   half = lineWidth * 0.5F;
		const Foundation::Vec2 low = glm::min(start, end) - half;
		const Foundation::Vec2 high = glm::max(start, end) + half;
		localStart = start - low;
		localEnd = end - low;
		setPosition(low);
		Component::setSize(high - low);
	}

	void Line::describe(nlohmann::json& node) const {
		node["style"] = detail::styleJson(std::nullopt, std::nullopt, style.color.value_or(kStroke));
	}

	void Line::render() {
		const std::array<Foundation::Vec2, 2> points{localStart, localEnd};
		Renderer::Primitives::drawPolyline({
			.points = points,
			.width = lineWidth,
			.color = detail::withOpacity(style.color.value_or(kStroke), opacity()),
			.cap = roundCaps ? Renderer::Primitives::LineCap::Round : Renderer::Primitives::LineCap::Butt,
			.id = getId().empty() ? nullptr : getId().c_str(),
			.key = drawKey(),
		});
	}

} // namespace ui
