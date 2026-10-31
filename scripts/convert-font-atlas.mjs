#!/usr/bin/env node
// Converts a font atlas JSON in salal's old layout (worldsim's tools/generate_sdf_atlas) to
// msdf-atlas-gen's, which FontRenderer reads. The PNG doesn't change.
//
//   node scripts/convert-font-atlas.mjs <atlas.json> [<out.json>]
//
// Writes <out.json>, or over <atlas.json> when no output is named. The old layout keyed glyphs by a
// one-character string, gave atlasBounds from 0 to 1 with the origin at the image's bottom, and
// gave the glyph's tight outline, which FontRenderer grew by half the distance range in the plane
// and the atlas and clamped to the atlas. msdf-atlas-gen's bounds already hold that half range, so
// the conversion grows them here, by the same amounts and with the same clamp, and the text lays
// out as it did. The glyph cells (each glyph's "atlas" rect) have no place in msdf-atlas-gen's
// format and are dropped.

import { readFileSync, writeFileSync } from 'node:fs';

const [inputPath, outputPath = inputPath] = process.argv.slice(2);
if (!inputPath || process.argv.length > 4) {
	console.error('usage: node scripts/convert-font-atlas.mjs <atlas.json> [<out.json>]');
	process.exit(2);
}

function fail(problem) {
	console.error(`${inputPath}: ${problem}`);
	process.exit(1);
}

const isNumber = (value) => typeof value === 'number' && Number.isFinite(value);
const isBounds = (value) =>
	value !== null && typeof value === 'object' && ['left', 'bottom', 'right', 'top'].every((side) => isNumber(value[side]));

let old;
try {
	old = JSON.parse(readFileSync(inputPath, 'utf8'));
} catch (error) {
	fail(error.message);
}
if (Array.isArray(old?.glyphs)) {
	fail('glyphs is an array, so this is msdf-atlas-gen JSON already');
}
const { atlas, metrics, glyphs } = old ?? {};
if (!atlas || !metrics || !glyphs || typeof glyphs !== 'object') {
	fail('needs atlas, metrics and glyphs');
}
for (const field of ['distanceRange', 'size', 'width', 'height']) {
	if (!isNumber(atlas[field]) || atlas[field] <= 0) fail(`atlas.${field} must be a positive number`);
}
for (const field of ['emSize', 'ascender', 'descender', 'lineHeight']) {
	if (!isNumber(metrics[field])) fail(`metrics.${field} must be a number`);
}

// FontRenderer's padding: half the range, in texels in the atlas and in em in the plane.
const halfRange = atlas.distanceRange * 0.5;
const padEm = halfRange / atlas.size;
const clamp = (value, high) => Math.min(Math.max(value, 0), high);

// Nine decimals is far finer than the float FontRenderer computes in, and keeps 0.05 - 0.0625 from
// printing as -0.012500000000000004.
const tidy = (value) => Number(value.toFixed(9));

const converted = [];
for (const [key, glyph] of Object.entries(glyphs)) {
	const codePoints = [...key];
	if (codePoints.length !== 1) fail(`glyph key ${JSON.stringify(key)} is not one character`);
	if (!isNumber(glyph?.advance)) fail(`glyph ${JSON.stringify(key)} has no advance`);
	const entry = { unicode: key.codePointAt(0), advance: glyph.advance };
	// As FontRenderer read it: geometry needs a cell, atlasBounds and plane bounds, or it's whitespace.
	if (glyph.atlas != null && glyph.atlasBounds != null && glyph.plane != null) {
		const { plane, atlasBounds } = glyph;
		if (!isBounds(plane) || !isBounds(atlasBounds)) fail(`glyph ${JSON.stringify(key)} has malformed bounds`);
		entry.planeBounds = {
			left: tidy(plane.left - padEm),
			bottom: tidy(plane.bottom - padEm),
			right: tidy(plane.right + padEm),
			top: tidy(plane.top + padEm),
		};
		entry.atlasBounds = {
			left: tidy(clamp(atlasBounds.left * atlas.width - halfRange, atlas.width)),
			bottom: tidy(clamp(atlasBounds.bottom * atlas.height - halfRange, atlas.height)),
			right: tidy(clamp(atlasBounds.right * atlas.width + halfRange, atlas.width)),
			top: tidy(clamp(atlasBounds.top * atlas.height + halfRange, atlas.height)),
		};
	}
	converted.push(entry);
}
converted.sort((a, b) => a.unicode - b.unicode);

const header = {
	atlas: {
		type: atlas.type ?? 'msdf',
		distanceRange: atlas.distanceRange,
		distanceRangeMiddle: 0,
		size: atlas.size,
		width: atlas.width,
		height: atlas.height,
		yOrigin: 'bottom',
	},
	metrics: {
		emSize: metrics.emSize,
		lineHeight: metrics.lineHeight,
		ascender: metrics.ascender,
		descender: metrics.descender,
	},
};

// One glyph a line, so a regenerated atlas diffs by glyph.
const lines = [
	'{',
	`\t"atlas": ${JSON.stringify(header.atlas)},`,
	`\t"metrics": ${JSON.stringify(header.metrics)},`,
	'\t"glyphs": [',
	converted.map((entry) => `\t\t${JSON.stringify(entry)}`).join(',\n'),
	'\t],',
	'\t"kerning": []',
	'}',
	'',
];
writeFileSync(outputPath, lines.join('\n'));
console.log(`${outputPath}: ${converted.length} glyphs`);
