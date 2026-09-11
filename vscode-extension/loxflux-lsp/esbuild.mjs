import esbuild from 'esbuild';

const watch = process.argv.includes('--watch');

/** @type {import('esbuild').BuildOptions} */
const common = {
	bundle: true,
	sourcemap: false,
	logLevel: 'info',
	target: 'node18',
	platform: 'node',
	external: ['vscode'],
};

async function main() {
	const client = esbuild.context({
		...common,
		entryPoints: ['src/client/extension.ts'],
		outfile: 'out/extension.js',
	});

	const server = esbuild.context({
		...common,
		entryPoints: ['src/server/server.ts'],
		outfile: 'out/server.js',
	});

	const test = esbuild.context({
		...common,
		entryPoints: ['src/test/run.ts'],
		outfile: 'out/test/run.js',
	});

	const contexts = await Promise.all([client, server, test]);

	if (watch) {
		await Promise.all(contexts.map((ctx) => ctx.watch()));
	} else {
		await Promise.all(contexts.map((ctx) => ctx.rebuild()));
		await Promise.all(contexts.map((ctx) => ctx.dispose()));
	}
}

main().catch((e) => {
	console.error(e);
	process.exit(1);
});
