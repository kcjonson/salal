#include "ui/detail/Grid.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace ui::detail {

	namespace {

		constexpr float kInfinity = std::numeric_limits<float>::infinity();

		// --- Placement ---

		// One axis of an item's placement in origin-zero lines (0 at the explicit grid's first line):
		// definite, or automatic with the span it takes.
		struct AxisPlacement {
			bool		 definite = false;
			std::int32_t start = 0;
			std::int32_t span = 1;
		};

		std::int32_t originZero(std::int16_t line, std::uint32_t explicitTracks) {
			return line > 0 ? line - 1 : static_cast<std::int32_t>(explicitTracks) + 1 + line;
		}

		std::int32_t spanOf(const GridPlacement& edge) {
			return std::max<std::int32_t>(edge.value, 1);
		}

		// 8.3 and its conflict handling, 8.3.1.
		AxisPlacement resolveAxis(const GridLines& lines, std::uint32_t explicitTracks) {
			using Kind = GridPlacement::Kind;
			const GridPlacement& start = lines.start;
			const GridPlacement& end = lines.end;
			if (start.kind == Kind::Line && end.kind == Kind::Line) {
				std::int32_t from = originZero(start.value, explicitTracks);
				std::int32_t to = originZero(end.value, explicitTracks);
				if (from > to) {
					std::swap(from, to);
				}
				return {.definite = true, .start = from, .span = std::max(to - from, 1)};
			}
			if (start.kind == Kind::Line) {
				return {
					.definite = true, .start = originZero(start.value, explicitTracks), .span = end.kind == Kind::Span ? spanOf(end) : 1
				};
			}
			if (end.kind == Kind::Line) {
				const std::int32_t count = start.kind == Kind::Span ? spanOf(start) : 1;
				return {.definite = true, .start = originZero(end.value, explicitTracks) - count, .span = count};
			}
			// With a span on both edges, the end's goes.
			const std::int32_t count = start.kind == Kind::Span ? spanOf(start) : (end.kind == Kind::Span ? spanOf(end) : 1);
			return {.definite = false, .start = 0, .span = count};
		}

		// The cells placed items cover: `stride` columns, rows added as items reach them.
		class Occupancy {
		  public:
			explicit Occupancy(std::uint32_t columns)
				: stride(columns) {}

			[[nodiscard]] bool fits(std::uint32_t column, std::uint32_t row, std::uint32_t columns, std::uint32_t rows) const {
				if (column + columns > stride) {
					return false;
				}
				for (std::uint32_t r = row; r < row + rows && r < rowCount; ++r) {
					for (std::uint32_t c = column; c < column + columns; ++c) {
						if (cells[(static_cast<std::size_t>(r) * stride) + c] != 0) {
							return false;
						}
					}
				}
				return true;
			}

			void occupy(const GridArea& area) {
				if (area.row.end() > rowCount) {
					rowCount = area.row.end();
					cells.resize(static_cast<std::size_t>(stride) * rowCount, 0);
				}
				for (std::uint32_t r = area.row.start; r < area.row.end(); ++r) {
					for (std::uint32_t c = area.column.start; c < area.column.end(); ++c) {
						cells[(static_cast<std::size_t>(r) * stride) + c] = 1;
					}
				}
			}

		  private:
			std::vector<std::uint8_t> cells;
			std::uint32_t			  stride;
			std::uint32_t			  rowCount = 0;
		};

		// --- Track sizing ---

		enum class MinKind : std::uint8_t { Fixed, Auto };
		enum class MaxKind : std::uint8_t { Fixed, Auto, Flex };

		struct Track {
			float	base = 0.0F;
			float	limit = kInfinity; // the growth limit
			float	maxValue = 0.0F;   // a fixed maximum in px, or the fr factor
			float	planned = 0.0F;
			float	incurred = 0.0F;
			MinKind minKind = MinKind::Auto;
			MaxKind maxKind = MaxKind::Auto;
			bool	infinitelyGrowable = false;
			bool	spanned = false; // by an item in the step being run
		};

		// The steps of 11.5 that distribute an item's space (11.5.1): which size grows, in which tracks,
		// toward which of its contributions.
		enum class Step : std::uint8_t { IntrinsicMinimums, MaxContentMinimums, IntrinsicMaximums, MaxContentMaximums };

		bool growsBase(Step step) {
			return step == Step::IntrinsicMinimums || step == Step::MaxContentMinimums;
		}

		struct Item {
			TrackRange tracks;
			float	   minimum = 0.0F; // its minimum contribution
			float	   minContent = 0.0F;
			float	   maxContent = 0.0F;
			float	   cap = kInfinity;	 // the spanned tracks' fixed maximums and gaps, when they all have one
			bool	   flexible = false; // it spans an fr track
		};

		// Its min-content and max-content limited by the fixed maximums it spans, never under its minimum.
		float limitedMinContent(const Item& item) {
			return std::max(std::min(item.minContent, item.cap), item.minimum);
		}

		float limitedMaxContent(const Item& item) {
			return std::max(std::min(item.maxContent, item.cap), item.minimum);
		}

		std::optional<float> resolveBreadth(float value, bool percent, std::optional<float> available) {
			if (!percent) {
				return std::max(value, 0.0F);
			}
			if (!available) {
				return std::nullopt;
			}
			return std::max(*available * value / 100.0F, 0.0F);
		}

		// 11.4.
		Track initialTrack(const TrackSizingFunction& function, std::optional<float> available) {
			Track track;
			if (!function.min.isAuto()) {
				if (const std::optional<float> minimum =
						resolveBreadth(function.min.value, function.min.unit == Length::Unit::Percent, available)) {
					track.minKind = MinKind::Fixed;
					track.base = *minimum;
				}
			}
			using Unit = MaxTrackSizingFunction::Unit;
			switch (function.max.unit) {
				case Unit::Px:
				case Unit::Percent:
					if (const std::optional<float> maximum =
							resolveBreadth(function.max.value, function.max.unit == Unit::Percent, available)) {
						// A maximum under the minimum is the minimum.
						track.maxKind = MaxKind::Fixed;
						track.maxValue = std::max(*maximum, track.base);
						track.limit = track.maxValue;
					}
					break;
				case Unit::Fr:
					track.maxKind = MaxKind::Flex;
					track.maxValue = std::max(function.max.value, 0.0F);
					break;
				case Unit::Auto:
					break;
			}
			return track;
		}

		class TrackSizer {
		  public:
			explicit TrackSizer(const GridTrackSizing& sizing)
				: available(sizing.available ? std::optional(std::max(*sizing.available, 0.0F)) : std::nullopt),
				  gap(std::max(sizing.gap, 0.0F)),
				  constrained(!available) {
				tracks.reserve(sizing.tracks.size());
				for (const TrackSizingFunction& function : sizing.tracks) {
					tracks.push_back(initialTrack(function, available));
				}
				items.reserve(sizing.items.size());
				for (const TrackContribution& contribution : sizing.items) {
					items.push_back(prepare(contribution));
				}
			}

			void run(std::vector<float>& sizes) {
				sizeToItemsInOneTrack();
				sizeToSpanningItems();
				sizeToItemsCrossingFlexibleTracks();
				for (Track& track : tracks) {
					if (track.limit == kInfinity) {
						track.limit = track.base;
					}
				}
				maximize();
				expandFlexibleTracks();
				stretchAutoTracks();
				sizes.resize(tracks.size());
				for (std::size_t i = 0; i < tracks.size(); ++i) {
					sizes[i] = tracks[i].base;
				}
			}

		  private:
			// An item's minimum contribution, which for CSS's automatic minimum hangs on the tracks it spans.
			Item prepare(const TrackContribution& contribution) const {
				Item item;
				item.tracks = contribution.tracks;
				item.minContent = std::max(contribution.minContent, 0.0F);
				item.maxContent = std::max(contribution.maxContent, item.minContent);
				bool  autoMinimum = false;
				bool  fixedMaximums = true;
				float fixedSum = gapsIn(item.tracks.count);
				for (const Track& track : spanned(item)) {
					autoMinimum = autoMinimum || track.minKind == MinKind::Auto;
					item.flexible = item.flexible || track.maxKind == MaxKind::Flex;
					fixedMaximums = fixedMaximums && track.maxKind == MaxKind::Fixed;
					fixedSum += track.maxKind == MaxKind::Fixed ? track.maxValue : 0.0F;
				}
				item.cap = fixedMaximums ? fixedSum : kInfinity;
				if (contribution.minimum) {
					item.minimum = std::max(*contribution.minimum, 0.0F);
				} else if (autoMinimum && (item.tracks.count == 1 || !item.flexible)) {
					item.minimum = std::min(item.minContent, item.cap);
				}
				return item;
			}

			[[nodiscard]] std::span<const Track> spanned(const Item& item) const {
				return std::span(tracks).subspan(item.tracks.start, item.tracks.count);
			}

			std::span<Track> spanned(const Item& item) { return std::span(tracks).subspan(item.tracks.start, item.tracks.count); }

			[[nodiscard]] float gapsIn(std::size_t count) const { return count > 1 ? gap * static_cast<float>(count - 1) : 0.0F; }

			[[nodiscard]] float used() const {
				float total = gapsIn(tracks.size());
				for (const Track& track : tracks) {
					total += track.base;
				}
				return total;
			}

			void raiseLimitsToBases() {
				for (Track& track : tracks) {
					track.limit = std::max(track.limit, track.base);
				}
			}

			// 11.5 step 2: items in one track, apart from fr tracks.
			void sizeToItemsInOneTrack() {
				for (const Item& item : items) {
					if (item.tracks.count != 1 || item.flexible) {
						continue;
					}
					Track& track = tracks[item.tracks.start];
					if (track.minKind == MinKind::Auto) {
						track.base = std::max(track.base, constrained ? limitedMinContent(item) : item.minimum);
					}
					if (track.maxKind == MaxKind::Auto) {
						track.limit = track.limit == kInfinity ? item.maxContent : std::max(track.limit, item.maxContent);
					}
				}
				raiseLimitsToBases();
			}

			// Step 3: items spanning several tracks and no fr track, a span length at a time, shortest first.
			void sizeToSpanningItems() {
				order.clear();
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].tracks.count > 1 && !items[i].flexible) {
						order.push_back(static_cast<std::uint32_t>(i));
					}
				}
				std::stable_sort(order.begin(), order.end(), [this](std::uint32_t a, std::uint32_t b) {
					return items[a].tracks.count < items[b].tracks.count;
				});
				for (std::size_t first = 0; first < order.size();) {
					std::size_t last = first + 1;
					while (last < order.size() && items[order[last]].tracks.count == items[order[first]].tracks.count) {
						++last;
					}
					const std::vector<std::uint32_t> group(
						order.begin() + static_cast<std::ptrdiff_t>(first), order.begin() + static_cast<std::ptrdiff_t>(last)
					);
					distribute(group, Step::IntrinsicMinimums, false);
					if (constrained) {
						distribute(group, Step::MaxContentMinimums, false);
					}
					raiseLimitsToBases();
					distribute(group, Step::IntrinsicMaximums, false);
					distribute(group, Step::MaxContentMaximums, false);
					for (Track& track : tracks) {
						track.infinitelyGrowable = false;
					}
					first = last;
				}
			}

			// Step 4: every item crossing an fr track at once, into the fr tracks alone, by their factors.
			// Their maximums aren't intrinsic, so only the minimum steps grow anything.
			void sizeToItemsCrossingFlexibleTracks() {
				std::vector<std::uint32_t> group;
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].flexible) {
						group.push_back(static_cast<std::uint32_t>(i));
					}
				}
				if (group.empty()) {
					return;
				}
				distribute(group, Step::IntrinsicMinimums, true);
				if (constrained) {
					distribute(group, Step::MaxContentMinimums, true);
				}
				raiseLimitsToBases();
			}

			[[nodiscard]] static bool affected(const Track& track, Step step, bool flexible) {
				if (flexible && track.maxKind != MaxKind::Flex) {
					return false;
				}
				return growsBase(step) ? track.minKind == MinKind::Auto : track.maxKind == MaxKind::Auto;
			}

			[[nodiscard]] float contribution(const Item& item, Step step) const {
				switch (step) {
					case Step::IntrinsicMinimums:
						return constrained ? limitedMinContent(item) : item.minimum;
					case Step::MaxContentMinimums:
						return limitedMaxContent(item);
					case Step::IntrinsicMaximums:
						return item.minContent;
					case Step::MaxContentMaximums:
						break;
				}
				return item.maxContent;
			}

			// The size a step grows, an infinite growth limit read as the base size.
			[[nodiscard]] static float affectedSize(const Track& track, Step step) {
				return growsBase(step) || track.limit == kInfinity ? track.base : track.limit;
			}

			// How far that size may grow before the track freezes.
			[[nodiscard]] static float room(const Track& track, Step step) {
				float limit = track.limit;
				if (!growsBase(step) && (track.limit == kInfinity || track.infinitelyGrowable)) {
					limit = kInfinity;
				}
				return limit - affectedSize(track, step) - track.incurred;
			}

			// 11.5.1 over a group of items, then each spanned track grows by its planned increase.
			void distribute(std::span<const std::uint32_t> group, Step step, bool flexible) {
				for (const std::uint32_t index : group) {
					for (Track& track : spanned(items[index])) {
						track.planned = 0.0F;
						track.spanned = true;
					}
				}
				for (const std::uint32_t index : group) {
					distributeItem(items[index], step, flexible);
				}
				for (Track& track : tracks) {
					if (!track.spanned) {
						continue;
					}
					track.spanned = false;
					if (growsBase(step)) {
						track.base += track.planned;
					} else if (affected(track, step, flexible)) {
						if (track.limit == kInfinity) {
							track.limit = track.base + track.planned;
							track.infinitelyGrowable = step == Step::IntrinsicMaximums;
						} else {
							track.limit += track.planned;
						}
					}
				}
			}

			void distributeItem(const Item& item, Step step, bool flexible) {
				const std::span<Track> span = spanned(item);
				float				   space = contribution(item, step) - gapsIn(span.size());
				for (Track& track : span) {
					space -= affectedSize(track, step);
					track.incurred = 0.0F;
				}
				if (space <= 0.0F) {
					return;
				}
				// Up to their limits: the affected tracks, then the others it spans (in step 4, the other fr
				// tracks); past them, the affected tracks with an auto maximum, or all of them without one.
				space = share(span, space, step, flexible, [&](const Track& track) { return affected(track, step, flexible); });
				if (space > 0.0F) {
					space = share(span, space, step, flexible, [&](const Track& track) {
						return !affected(track, step, flexible) && (!flexible || track.maxKind == MaxKind::Flex);
					});
				}
				if (space > 0.0F) {
					const bool anyAuto = std::any_of(span.begin(), span.end(), [&](const Track& track) {
						return affected(track, step, flexible) && track.maxKind == MaxKind::Auto;
					});
					shareBeyondLimits(span, space, flexible, [&](const Track& track) {
						return affected(track, step, flexible) && (!anyAuto || track.maxKind == MaxKind::Auto);
					});
				}
				for (Track& track : span) {
					track.planned = std::max(track.planned, track.incurred);
				}
			}

			// Shares `space` over the tracks `takes` picks, equally, freezing each at its limit, and returns
			// what is left. In step 4 the shares go by fr factor, and an fr track's limit is still infinite.
			template <typename Takes>
			float share(std::span<Track> span, float space, Step step, bool flexible, Takes takes) {
				picked.clear();
				for (std::size_t i = 0; i < span.size(); ++i) {
					if (takes(span[i])) {
						picked.push_back(static_cast<std::uint32_t>(i));
					}
				}
				if (picked.empty()) {
					return space;
				}
				if (flexible) {
					shareByFactor(span, space);
					return 0.0F;
				}
				// The tightest first: each takes an equal share of what's left, or its room if that's less.
				std::sort(picked.begin(), picked.end(), [&](std::uint32_t a, std::uint32_t b) {
					return room(span[a], step) < room(span[b], step);
				});
				auto remaining = static_cast<float>(picked.size());
				for (const std::uint32_t index : picked) {
					const float grown = std::min(std::max(room(span[index], step), 0.0F), space / remaining);
					span[index].incurred += grown;
					space -= grown;
					remaining -= 1.0F;
				}
				return space;
			}

			template <typename Takes>
			void shareBeyondLimits(std::span<Track> span, float space, bool flexible, Takes takes) {
				picked.clear();
				for (std::size_t i = 0; i < span.size(); ++i) {
					if (takes(span[i])) {
						picked.push_back(static_cast<std::uint32_t>(i));
					}
				}
				if (picked.empty()) {
					return;
				}
				if (flexible) {
					shareByFactor(span, space);
					return;
				}
				const float each = space / static_cast<float>(picked.size());
				for (const std::uint32_t index : picked) {
					span[index].incurred += each;
				}
			}

			// Over `picked` by fr factor; factors summing under 1 take that much of it by factor and the
			// rest equally.
			void shareByFactor(std::span<Track> span, float space) {
				float factors = 0.0F;
				for (const std::uint32_t index : picked) {
					factors += span[index].maxValue;
				}
				const float equally = factors < 1.0F ? space * (1.0F - factors) / static_cast<float>(picked.size()) : 0.0F;
				for (const std::uint32_t index : picked) {
					span[index].incurred +=
						factors < 1.0F ? (space * span[index].maxValue) + equally : space * span[index].maxValue / factors;
				}
			}

			// 11.6: free space grows every base size up to its growth limit; sized to content, all the way.
			void maximize() {
				if (!available) {
					for (Track& track : tracks) {
						track.base = track.limit;
					}
					return;
				}
				float free = *available - used();
				if (free <= 0.0F) {
					return;
				}
				picked.clear();
				for (std::size_t i = 0; i < tracks.size(); ++i) {
					picked.push_back(static_cast<std::uint32_t>(i));
				}
				std::sort(picked.begin(), picked.end(), [this](std::uint32_t a, std::uint32_t b) {
					return tracks[a].limit - tracks[a].base < tracks[b].limit - tracks[b].base;
				});
				auto remaining = static_cast<float>(picked.size());
				for (const std::uint32_t index : picked) {
					Track&		track = tracks[index];
					const float grown = std::min(std::max(track.limit - track.base, 0.0F), free / remaining);
					track.base += grown;
					free -= grown;
					remaining -= 1.0F;
				}
			}

			// 11.7.1: the size of 1fr filling `space` across `first..first + count`, an fr track whose share
			// would fall under its base size held at its base size.
			float frSize(std::size_t first, std::size_t count, float space) {
				float leftover = space - gapsIn(count);
				frozen.assign(count, false);
				for (std::size_t i = 0; i < count; ++i) {
					const Track& track = tracks[first + i];
					if (track.maxKind != MaxKind::Flex) {
						frozen[i] = true;
						leftover -= track.base;
					}
				}
				while (true) {
					float factors = 0.0F;
					for (std::size_t i = 0; i < count; ++i) {
						factors += frozen[i] ? 0.0F : tracks[first + i].maxValue;
					}
					const float size = leftover / std::max(factors, 1.0F);
					bool		restart = false;
					for (std::size_t i = 0; i < count; ++i) {
						const Track& track = tracks[first + i];
						if (!frozen[i] && size * track.maxValue < track.base) {
							frozen[i] = true;
							leftover -= track.base;
							restart = true;
						}
					}
					if (!restart) {
						return size;
					}
				}
			}

			// 11.7.
			void expandFlexibleTracks() {
				const bool any =
					std::any_of(tracks.begin(), tracks.end(), [](const Track& track) { return track.maxKind == MaxKind::Flex; });
				if (!any) {
					return;
				}
				float fraction = 0.0F;
				if (available) {
					if (*available - used() > 0.0F) {
						fraction = frSize(0, tracks.size(), *available);
					}
				} else {
					for (const Track& track : tracks) {
						if (track.maxKind == MaxKind::Flex) {
							fraction = std::max(fraction, track.maxValue > 1.0F ? track.base / track.maxValue : track.base);
						}
					}
					for (const Item& item : items) {
						if (item.flexible) {
							fraction = std::max(fraction, frSize(item.tracks.start, item.tracks.count, item.maxContent));
						}
					}
				}
				for (Track& track : tracks) {
					if (track.maxKind == MaxKind::Flex) {
						track.base = std::max(track.base, fraction * track.maxValue);
					}
				}
			}

			// 11.8: what's left goes to the auto maximums, equally.
			void stretchAutoTracks() {
				if (!available) {
					return;
				}
				const float free = *available - used();
				const auto	autoTracks =
					std::count_if(tracks.begin(), tracks.end(), [](const Track& track) { return track.maxKind == MaxKind::Auto; });
				if (free <= 0.0F || autoTracks == 0) {
					return;
				}
				const float share = free / static_cast<float>(autoTracks);
				for (Track& track : tracks) {
					if (track.maxKind == MaxKind::Auto) {
						track.base += share;
					}
				}
			}

			std::optional<float>	   available;
			float					   gap;
			bool					   constrained; // sized to content: CSS's max-content constraint
			std::vector<Track>		   tracks;
			std::vector<Item>		   items;
			std::vector<std::uint32_t> order;
			std::vector<std::uint32_t> picked;
			std::vector<bool>		   frozen;
		};

	} // namespace

	void placeGridItems(const GridPlacementArgs& args, PlacedGrid& grid) {
		const std::size_t		   count = args.items.size();
		std::vector<AxisPlacement> columns(count);
		std::vector<AxisPlacement> rows(count);
		std::int32_t			   columnStart = 0;
		auto					   columnEnd = static_cast<std::int32_t>(args.explicitColumns);
		std::int32_t			   rowStart = 0;
		auto					   rowEnd = static_cast<std::int32_t>(args.explicitRows);
		std::int32_t			   autoSpan = 0; // the widest span of an item automatic on both axes
		std::int32_t			   rowLockedSpans = 0;
		for (std::size_t i = 0; i < count; ++i) {
			columns[i] = resolveAxis(args.items[i].column, args.explicitColumns);
			rows[i] = resolveAxis(args.items[i].row, args.explicitRows);
			if (columns[i].definite) {
				columnStart = std::min(columnStart, columns[i].start);
				columnEnd = std::max(columnEnd, columns[i].start + columns[i].span);
			} else if (rows[i].definite) {
				rowLockedSpans += columns[i].span;
			} else {
				autoSpan = std::max(autoSpan, columns[i].span);
			}
			if (rows[i].definite) {
				rowStart = std::min(rowStart, rows[i].start);
				rowEnd = std::max(rowEnd, rows[i].start + rows[i].span);
			}
		}

		// From here, tracks count from the implicit grid's first line.
		grid.columnsBefore = static_cast<std::uint32_t>(-columnStart);
		grid.rowsBefore = static_cast<std::uint32_t>(-rowStart);
		auto columnCount = static_cast<std::uint32_t>(columnEnd - columnStart);
		auto rowCount = static_cast<std::uint32_t>(rowEnd - rowStart);
		// A row-locked item lands past what its row already holds, so together they add at most their spans.
		Occupancy occupied(std::max(columnCount + static_cast<std::uint32_t>(rowLockedSpans), static_cast<std::uint32_t>(autoSpan)));
		grid.areas.assign(count, {});
		const auto put = [&](std::size_t i, std::uint32_t column, std::uint32_t row) {
			grid.areas[i] = {
				.column = {.start = column, .count = static_cast<std::uint32_t>(columns[i].span)},
				.row = {.start = row, .count = static_cast<std::uint32_t>(rows[i].span)},
			};
			occupied.occupy(grid.areas[i]);
			columnCount = std::max(columnCount, grid.areas[i].column.end());
			rowCount = std::max(rowCount, grid.areas[i].row.end());
		};

		// 1. Items definite on both axes.
		for (std::size_t i = 0; i < count; ++i) {
			if (columns[i].definite && rows[i].definite) {
				put(i, static_cast<std::uint32_t>(columns[i].start - columnStart), static_cast<std::uint32_t>(rows[i].start - rowStart));
			}
		}

		// 2. Items locked to a row: each at the first column from the explicit grid's first line where it
		//    fits, past whatever this step put in that row before it.
		std::vector<std::uint32_t> rowCursor(rowCount, grid.columnsBefore);
		for (std::size_t i = 0; i < count; ++i) {
			if (columns[i].definite || !rows[i].definite) {
				continue;
			}
			const auto	  row = static_cast<std::uint32_t>(rows[i].start - rowStart);
			const auto	  columnSpan = static_cast<std::uint32_t>(columns[i].span);
			std::uint32_t column = rowCursor[row];
			while (!occupied.fits(column, row, columnSpan, static_cast<std::uint32_t>(rows[i].span))) {
				++column;
			}
			put(i, column, row);
			rowCursor[row] = column + columnSpan;
		}

		// 3. Columns enough for the widest span left to place.
		columnCount = std::max(columnCount, static_cast<std::uint32_t>(autoSpan));

		// 4. The rest, sparse: the cursor only moves on.
		std::uint32_t cursorColumn = 0;
		std::uint32_t cursorRow = 0;
		for (std::size_t i = 0; i < count; ++i) {
			if (rows[i].definite) {
				continue;
			}
			const auto columnSpan = static_cast<std::uint32_t>(columns[i].span);
			const auto rowSpan = static_cast<std::uint32_t>(rows[i].span);
			if (columns[i].definite) {
				const auto column = static_cast<std::uint32_t>(columns[i].start - columnStart);
				if (column < cursorColumn) {
					++cursorRow;
				}
				cursorColumn = column;
				while (!occupied.fits(cursorColumn, cursorRow, columnSpan, rowSpan)) {
					++cursorRow;
				}
			} else {
				while (true) {
					if (cursorColumn + columnSpan > columnCount) {
						++cursorRow;
						cursorColumn = 0;
					} else if (occupied.fits(cursorColumn, cursorRow, columnSpan, rowSpan)) {
						break;
					} else {
						++cursorColumn;
					}
				}
			}
			put(i, cursorColumn, cursorRow);
		}

		grid.columns = columnCount;
		grid.rows = rowCount;
	}

	void gridTracks(
		std::span<const TrackSizingFunction> templateTracks,
		std::uint32_t						 before,
		std::uint32_t						 count,
		std::vector<TrackSizingFunction>&	 tracks
	) {
		tracks.assign(count, TrackSizingFunction{});
		for (std::size_t i = 0; i < templateTracks.size() && before + i < count; ++i) {
			tracks[before + i] = templateTracks[i];
		}
	}

	void sizeGridTracks(const GridTrackSizing& sizing, std::vector<float>& sizes) {
		TrackSizer(sizing).run(sizes);
	}

	float gridExtent(std::span<const float> sizes, float gap) {
		float extent = sizes.size() > 1 ? std::max(gap, 0.0F) * static_cast<float>(sizes.size() - 1) : 0.0F;
		for (const float size : sizes) {
			extent += size;
		}
		return extent;
	}

} // namespace ui::detail
