/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - offset <-> LSP position conversion.
 */

import { Position, Range } from 'vscode-languageserver-textdocument';

export class Positions {
	private readonly lineStarts: number[];

	constructor(private readonly source: string) {
		const starts: number[] = [0];
		for (let i = 0; i < source.length; i++) {
			if (source[i] === '\n') {
				starts.push(i + 1);
			}
		}
		this.lineStarts = starts;
	}

	positionAt(offset: number): Position {
		const clamped = Math.max(0, Math.min(offset, this.source.length));

		// binary search for the line
		let low = 0;
		let high = this.lineStarts.length - 1;
		while (low < high) {
			const mid = (low + high + 1) >> 1;
			if (this.lineStarts[mid] <= clamped) {
				low = mid;
			} else {
				high = mid - 1;
			}
		}

		const line = low;
		const character = clamped - this.lineStarts[line];
		return { line, character };
	}

	range(start: number, end: number): Range {
		return {
			start: this.positionAt(start),
			end: this.positionAt(end),
		};
	}

	offsetAt(position: Position): number {
		const line = Math.max(0, Math.min(position.line, this.lineStarts.length - 1));
		const lineStart = this.lineStarts[line];
		const lineEnd = line + 1 < this.lineStarts.length ? this.lineStarts[line + 1] - 1 : this.source.length;
		return Math.max(lineStart, Math.min(position.character + lineStart, lineEnd));
	}

	/** 0-based line index of an offset */
	lineOf(offset: number): number {
		return this.positionAt(offset).line;
	}
}
