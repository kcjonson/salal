#pragma once

#include "Types.h"
#include "graphics/Color.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace renderer {

	/// A loaded SVG shape with flattened paths ready for tessellation
	struct LoadedSVGShape {
		std::string				id;				   ///< The shape's SVG `id` attribute ("" if none); the part tag for rigging
		std::vector<VectorPath> paths;			   ///< Flattened polygon paths (Beziers already linearized)
		bool					hasFill{true};	   ///< Whether the shape has a fill (false for stroke-only)
		Foundation::Color		fillColor;		   ///< Solid fill color (and fallback when a gradient is ignored)
		GradientFill			gradient;		   ///< Gradient fill; type == None for solid-color shapes
		bool					hasStroke{false};  ///< Whether the shape has a stroke outline
		Foundation::Color		strokeColor;	   ///< Stroke color (solid)
		float					strokeWidth{0.0F}; ///< Stroke width in SVG user units
		float					width{0.0F};	   ///< Original SVG width
		float					height{0.0F};	   ///< Original SVG height
	};

	/// Load an SVG file and convert to VectorPaths ready for tessellation: the file's bytes, read
	/// whole, through the overload below. A game resolves the path itself (Foundation::findResource
	/// searches its resource roots and the executable's folder); SVGLoader takes no asset root.
	///
	/// @param filepath Path to the SVG file
	/// @param curveTolerance Bezier flattening tolerance (smaller = more vertices, smoother curves)
	///                       Recommended: 0.5-1.0 for screen rendering
	/// @param outShapes Output vector of loaded shapes with fill colors
	/// @return true if loading succeeded, false on error
	bool loadSVG(const std::string& filepath, float curveTolerance, std::vector<LoadedSVGShape>& outShapes);

	/// Load an SVG held in memory: its bytes, as salal_art() embeds them (<namespace>::art(name)) or as
	/// a game read them. NanoSVG parses a copy (it writes into its buffer), then every cubic Bezier is
	/// flattened with flattenCubicBezier(), so the VectorPaths go straight to Tessellator::Tessellate().
	/// `name` is what the log calls it.
	bool loadSVG(
		std::span<const std::byte>	 svg,
		float						 curveTolerance,
		std::vector<LoadedSVGShape>& outShapes,
		std::string_view			 name = "(memory)"
	);

	/// Tessellate a loaded SVG shape into colored triangles, appended to outMesh.
	///
	/// Per-vertex colors are baked from the shape's fill: a solid color fills every vertex, while a
	/// gradient is evaluated per output vertex (radial fills get an inserted centroid sample so they
	/// show a center, not just the edge color). Vertices stay in the SVG's own coordinate space;
	/// callers scale/position the resulting mesh afterward. Shapes are appended with index offset, so
	/// the same outMesh can accumulate many shapes.
	void appendShapeMesh(const LoadedSVGShape& shape, TessellatedMesh& outMesh);

} // namespace renderer
