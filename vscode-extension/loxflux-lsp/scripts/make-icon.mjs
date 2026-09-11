/*
 * Generates icon.png: white lambda glyph on an indigo gradient rounded square.
 * Pure Node (zlib PNG encoder) — no image tooling required.
 */
import { deflateSync } from 'zlib';
import { writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const outPath = join(dirname(fileURLToPath(import.meta.url)), '..', 'icon.png');

const S = 256;
const px = Buffer.alloc(S * S * 4);

function blend(x, y, r, g, b, a) {
	const i = (y * S + x) * 4;
	const ia = a / 255;
	const pa = px[i + 3] / 255;
	const outA = ia + pa * (1 - ia);
	if (outA <= 0) return;
	px[i] = Math.round((r * ia + px[i] * pa * (1 - ia)) / outA);
	px[i + 1] = Math.round((g * ia + px[i + 1] * pa * (1 - ia)) / outA);
	px[i + 2] = Math.round((b * ia + px[i + 2] * pa * (1 - ia)) / outA);
	px[i + 3] = Math.round(outA * 255);
}

// ---- background: vertical gradient, rounded corners ----------------------

const TOP = [124, 108, 240]; // #7C6CF0
const BOTTOM = [72, 52, 212]; // #4834D4
const R = 52; // corner radius

function roundedSDF(x, y) {
	const cx = Math.min(Math.max(x, R), S - R);
	const cy = Math.min(Math.max(y, R), S - R);
	return Math.hypot(x - cx, y - cy) - R;
}

for (let y = 0; y < S; y++) {
	const t = y / (S - 1);
	const r = TOP[0] + (BOTTOM[0] - TOP[0]) * t;
	const g = TOP[1] + (BOTTOM[1] - TOP[1]) * t;
	const b = TOP[2] + (BOTTOM[2] - TOP[2]) * t;
	for (let x = 0; x < S; x++) {
		const d = roundedSDF(x + 0.5, y + 0.5);
		const cov = Math.max(0, Math.min(1, 0.5 - d));
		if (cov > 0) {
			blend(x, y, r, g, b, Math.round(cov * 255));
		}
	}
}

// ---- lambda glyph: two anti-aliased strokes -------------------------------

function stroke(ax, ay, bx, by, halfW) {
	const len = Math.hypot(bx - ax, by - ay);
	const ux = (bx - ax) / len;
	const uy = (by - ay) / len;
	const minX = Math.max(0, Math.floor(Math.min(ax, bx) - halfW - 2));
	const maxX = Math.min(S - 1, Math.ceil(Math.max(ax, bx) + halfW + 2));
	const minY = Math.max(0, Math.floor(Math.min(ay, by) - halfW - 2));
	const maxY = Math.min(S - 1, Math.ceil(Math.max(ay, by) + halfW + 2));

	for (let y = minY; y <= maxY; y++) {
		for (let x = minX; x <= maxX; x++) {
			const p = x + 0.5 - ax;
			const q = y + 0.5 - ay;
			const t = Math.max(0, Math.min(len, p * ux + q * uy));
			const dx = p - ux * t;
			const dy = q - uy * t;
			const dist = Math.hypot(dx, dy);
			const cov = Math.max(0, Math.min(1, halfW - dist + 0.5));
			if (cov > 0) {
				blend(x, y, 255, 255, 255, Math.round(cov * 255));
			}
		}
	}
}

// main leg: top-left -> bottom-right
stroke(92, 64, 198, 212, 14);
// branch: top-right -> joins the main leg (lambda shape)
stroke(188, 64, 140, 132, 14);

// ---- encode PNG -----------------------------------------------------------

function crc32(buf) {
	let crc = 0xffffffff;
	for (let n = 0; n < buf.length; n++) {
		let c = (crc ^ buf[n]) & 0xff;
		for (let k = 0; k < 8; k++) {
			c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		}
		crc = (crc >>> 8) ^ c;
	}
	return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
	const len = Buffer.alloc(4);
	len.writeUInt32BE(data.length);
	const t = Buffer.from(type, 'ascii');
	const crc = Buffer.alloc(4);
	crc.writeUInt32BE(crc32(Buffer.concat([t, data])));
	return Buffer.concat([len, t, data, crc]);
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(S, 0);
ihdr.writeUInt32BE(S, 4);
ihdr[8] = 8; // bit depth
ihdr[9] = 6; // color type RGBA

const stride = S * 4 + 1;
const raw = Buffer.alloc(S * stride);
for (let y = 0; y < S; y++) {
	raw[y * stride] = 0; // filter: none
	px.copy(raw, y * stride + 1, y * S * 4, (y + 1) * S * 4);
}

const png = Buffer.concat([
	Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
	chunk('IHDR', ihdr),
	chunk('IDAT', deflateSync(raw, { level: 9 })),
	chunk('IEND', Buffer.alloc(0)),
]);

writeFileSync(outPath, png);
console.log(`wrote ${outPath} (${png.length} bytes)`);
