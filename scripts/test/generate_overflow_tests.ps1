# Generates the large stress/regression .lox cases into generated/.
# These files are too big to hand-write; regenerate any time with:
#   powershell -File generate_overflow_tests.ps1
#
# Each case carries its expectations in the leading // header, parsed by
# run_tests.ps1 (exit / out / err / err!).

$ErrorActionPreference = "Stop"
$genDir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "generated"
New-Item -ItemType Directory -Path $genDir -Force | Out-Null

function Write-Test([string]$name, [string]$content) {
	$path = Join-Path $genDir $name
	[System.IO.File]::WriteAllText($path, $content)
	Write-Output ("  wrote {0} ({1} bytes)" -f $name, (Get-Item $path).Length)
}

$nl = "`n"

# ---- break/continue overflow guards -------------------------------------

Write-Test "g_break_flood.lox" (
	"// exit: 65`n// err: Too many break statements in one loop.`n" +
	"var a = false;`n while (a) {`n" + ("  break;`n" * 40000) + "}`n")

Write-Test "g_break_32768.lox" (
	"// exit: 65`n// err: Loop body too large.`n// err!: Too many break statements`n" +
	"var a = false;`n while (a) {`n" + ("  break;`n" * 32768) + "}`n")

Write-Test "g_break_32769.lox" (
	"// exit: 65`n// err: Too many break statements in one loop.`n" +
	"var a = false;`n while (a) {`n" + ("  break;`n" * 32769) + "}`n")

Write-Test "g_continue_flood.lox" (
	"// exit: 65`n// err: Loop body too large.`n" +
	"var a = false;`n while (a) {`n" + ("  continue;`n" * 40000) + "}`n")

Write-Test "g_big_body_break.lox" (
	"// exit: 65`n// err: Loop body too large.`n" +
	"var a = false;`n while (a) {`n  break;`n" + ("  a = 1;`n" * 10000) + "}`n")

Write-Test "g_big_body_continue.lox" (
	"// exit: 65`n// err: Loop body too large.`n" +
	"var a = false;`n while (a) {`n" + ("  a = 1;`n" * 10000) + "  continue;`n}`n")

# ---- parser recursion depth guard (MAX_PARSE_DEPTH = 512) -----------------

Write-Test "g_deep_parens.lox" (
	"// exit: 65`n// err: Code nesting is too deep.`n" +
	"print " + ("(" * 30000) + "1" + (")" * 30000) + ";`n")

Write-Test "g_deep_unary.lox" (
	"// exit: 65`n// err: Code nesting is too deep.`n" +
	"print " + ("!" * 30000) + "true;`n")

Write-Test "g_deep_blocks.lox" (
	"// exit: 65`n// err: Code nesting is too deep.`n" +
	("  {`n" * 30000) + ("  }`n" * 30000))

Write-Test "g_deep_and_chain.lox" (
	"// exit: 65`n// err: Code nesting is too deep.`n" +
	"var a = true;`n print a" + (" and a" * 30000) + ";`n")

Write-Test "g_deep_branch_cases.lox" (
	"// exit: 65`n// err: Code nesting is too deep.`n" +
	"var x = 0;`n branch {`n" +
	(("  x: print 1;`n") * 30000) +
	"  none: print 2;`n }`n")

# ---- nesting below the guard must still work ------------------------------

Write-Test "g_nesting_ok.lox" (
	"// exit: 0`n// out: 2`n// out: block ok`n// out: true`n// out: false`n" +
	"print " + ("(" * 100) + "1 + 1" + (")" * 100) + ";`n" +
	("{`n" * 100) + "  print `"block ok`";`n" + ("}`n" * 100) +
	"var t = true;`n print t" + (" and t" * 100) + ";`n" +
	"print " + ("!" * 100) + "false;`n")

# ---- compiler limit guards -------------------------------------------------

Write-Test "g_too_many_params.lox" (
	"// exit: 65`n// err: Can't have more than 255 parameters.`n" +
	"fun f(" + ((1..256 | ForEach-Object { "p$_" }) -join ", ") + ") {`n}`n")

Write-Test "g_too_many_args.lox" (
	"// exit: 65`n// err: Can't have more than 255 arguments.`n" +
	"fun f() {`n  return 0;`n}`n" +
	"f(" + ((1..256 | ForEach-Object { "$_" }) -join ", ") + ");`n")

Write-Test "g_too_many_locals.lox" (
	"// exit: 65`n// err: Too many nested local variables in scope.`n" +
	"fun f() {`n" + ((1..1025 | ForEach-Object { "  var v$_ = 1;" }) -join "`n") + "`n}`n")

Write-Test "g_too_many_upvalues.lox" (
	"// exit: 65`n// err: Too many closure variables in function.`n" +
	"fun outer() {`n" +
	((1..257 | ForEach-Object { "  var v$_ = 1;" }) -join "`n") + "`n" +
	"  fun inner() {`n" +
	((1..257 | ForEach-Object { "    print v$_;" }) -join "`n") + "`n" +
	"  }`n  return inner;`n}`n")

# 65537 distinct number literals in one function overflow the 16-bit
# per-function number-constant pool.
Write-Test "g_number_consts.lox" (
	"// exit: 65`n// err: Too many constants in function.`n" +
	"fun f() {`n  var s = 0;`n" +
	((1..65537 | ForEach-Object { "  s = s + $_;" }) -join "`n") + "`n  return s;`n}`n")

Write-Output "done."
