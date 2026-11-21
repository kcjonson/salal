#pragma once

#include "Types.h"
#include "graphics/Color.h"
#include "math/AffineTransform2D.h"

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

	/// SVG's `vector-effect` for a shape's stroke under a transform.
	enum class VectorEffect {
		None,			 ///< The stroke's outline is transformed with the geometry, so a skew or scale thins it
		NonScalingStroke ///< The stroke is built after the transform, at the shape's own stroke width
	};

	/// How appendShapeMesh places and colours a shape, with CSS and SVG's names.
	struct ShapeMeshArgs {
		Foundation::AffineTransform2D transform{};						 ///< CSS/SVG `transform: matrix(a, b, c, d, e, f)`
		VectorEffect				  vectorEffect = VectorEffect::None; ///< SVG `vector-effect`
		float						  brightness = 1.0F;				 ///< CSS `filter: brightness()`: finite, >= 0
	};

	/// Tessellate a loaded SVG shape into colored triangles, appended to outMesh.
	///
	/// Per-vertex colors are baked from the shape's fill: a solid color fills every vertex, while a
	/// gradient is evaluated per output vertex (radial fills get an inserted centroid sample so they
	/// show a center, not just the edge color). Shapes are appended with index offset, so the same
	/// outMesh can accumulate many shapes.
	///
	/// `args.transform` maps the shape into the output space. The fill is tessellated and coloured in
	/// the shape's own space and its vertices are then mapped: an affine map keeps each vertex's
	/// barycentric weights, so a gradient looks painted on the shape and then transformed, with no
	/// inverse taken. With VectorEffect::None the stroke band is built in the shape's space and then
	/// mapped; with NonScalingStroke the centreline is mapped and the band built at strokeWidth in the
	/// output space, so a line keeps its width on a skewed face. `args.brightness` multiplies each
	/// vertex's colour (the solid fill, the gradient as evaluated there, the stroke) in RGB, clamped
	/// to [0, 1], alpha untouched, as CSS applies the filter to the painted colour. It must be finite
	/// and at least 0: a debug build asserts it, a release build doesn't check (a negative value
	/// clamps to black, a non-finite one gives non-finite colours). The defaults (identity, None, 1) leave the shape in its SVG space as it
	/// was drawn.
	///
	/// A near-singular transform (a face seen almost edge-on) gives degenerate fill triangles and,
	/// with NonScalingStroke, a band of full width round a collapsed centreline, never further than
	/// half the stroke width from it (joins are bevelled, so there are no miter spikes); nothing comes
	/// out non-finite. A caller drawing onto such faces skips the art below a minimum projected width
	/// and clips what it draws to the face. Geometry that would take the mesh past 65,535 vertices
	/// (16-bit indices) is dropped, the fill or one subpath's stroke whole, with a warning.
	void appendShapeMesh(const LoadedSVGShape& shape, TessellatedMesh& outMesh, const ShapeMeshArgs& args = {});

} // namespace renderer
