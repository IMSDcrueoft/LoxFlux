-- Lua 5.4 equivalents of the README benchmarks (matches scripts/benchmark/lfx/*.lfx semantics)
-- usage: lua bench.lua [name|all]
--
-- timing note: os.clock() is the highest-resolution timer available in the Lua
-- standard library (CPU seconds, float). All benchmarks report wall-clock-like
-- elapsed CPU ms to match the other interpreters' reports.
--
-- fairness note: the loop benchmarks intentionally use hand-written `while`
-- loops with explicit counters instead of Lua's numeric `for`, because numeric
-- for compiles to the dedicated FORPREP/FORLOOP integer instructions -- the
-- .lfx originals hand-roll the counter too, so both sides do the same work.
-- A numeric-for variant (`loopfor`) is also provided for reference, since the
-- README's historical Lua numbers for `loop 1e8` appear to match that form.

local which = arg[1] or "all"

local function now()
	return os.clock()
end

local function report(name, elapsed_s)
	print(string.format("%s: %.0fms", name, elapsed_s * 1000))
end

-- ---------- fib ----------
local function make_fib()
	local function fib(n)
		if n < 2 then return n end
		return fib(n - 2) + fib(n - 1)
	end
	return fib
end

local function bench_fib(n, expected)
	local fib = make_fib()
	local start = now()
	local r = fib(n)
	local elapsed = now() - start
	if expected ~= nil then
		print(r == expected)
	end
	report("fib" .. n, elapsed)
end

-- ---------- loop 1e8 ----------
local function bench_loop()
	local start = now()
	local i = 0
	while i < 100000000 do
		i = i + 1
	end
	report("loop 1e8", now() - start)
end

-- ---------- global loop 1e8 ----------
-- `i` is intentionally a real global (no `local`), matching the .lfx original
local function bench_global_loop()
	local start = now()
	i = 0
	while i < 100000000 do
		i = i + 1
	end
	report("global loop 1e8", now() - start)
end

-- ---------- loop 1e8 (numeric for, reference only) ----------
-- uses Lua's dedicated FORPREP/FORLOOP integer instructions; the counter lives
-- in a virtual register and the loop body is empty, so this measures Lua's
-- hand-optimized counting loop rather than a hand-written one. There is no
-- global variant: numeric for always uses a local loop variable.
local function bench_loop_for()
	local start = now()
	for _ = 0, 99999999 do
	end
	report("loop 1e8 (numeric for)", now() - start)
end

-- ---------- binary_trees ----------
local function bench_binary_trees()
	local Tree = {}
	Tree.__index = Tree

	function Tree.new(item, depth)
		local self = setmetatable({}, Tree)
		self.item = item
		self.depth = depth
		if depth > 0 then
			local item2 = item + item
			depth = depth - 1
			self.left = Tree.new(item2 - 1, depth)
			self.right = Tree.new(item2, depth)
		else
			self.left = nil
			self.right = nil
		end
		return self
	end

	function Tree:check()
		if self.left == nil then
			return self.item
		end
		return self.item + self.left:check() - self.right:check()
	end

	local min_depth = 4
	local max_depth = 14
	local stretch_depth = max_depth + 1
	local start = now()

	Tree.new(0, stretch_depth):check()
	local long_lived = Tree.new(0, max_depth)

	-- iterations = 2 ** max_depth, hand-rolled like the .lfx original
	local iterations = 1
	local d = 0
	while d < max_depth do
		iterations = iterations * 2
		d = d + 1
	end

	local depth = min_depth
	while depth < stretch_depth do
		local check = 0
		local i = 1
		while i <= iterations do
			check = check + Tree.new(i, depth):check() + Tree.new(-i, depth):check()
			i = i + 1
		end
		-- iterations = iterations / 4 (always an exact power of two here, so // keeps the value identical)
		iterations = iterations // 4
		depth = depth + 2
	end
	long_lived:check()
	report("binary_trees", now() - start)
end

-- ---------- instantiation ----------
local function bench_instantiation()
	local Foo = {}
	Foo.__index = Foo
	function Foo.new() return setmetatable({}, Foo) end

	local start = now()
	local i = 0
	while i < 500000 do
		Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new()
		Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new()
		Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new(); Foo.new()
		i = i + 1
	end
	report("instantiation", now() - start)
end

-- ---------- invocation ----------
local function bench_invocation()
	local Foo = {}
	Foo.__index = Foo
	function Foo.new() return setmetatable({}, Foo) end
	function Foo:method0() end function Foo:method1() end function Foo:method2() end
	function Foo:method3() end function Foo:method4() end function Foo:method5() end
	function Foo:method6() end function Foo:method7() end function Foo:method8() end
	function Foo:method9() end function Foo:method10() end function Foo:method11() end
	function Foo:method12() end function Foo:method13() end function Foo:method14() end
	function Foo:method15() end function Foo:method16() end function Foo:method17() end
	function Foo:method18() end function Foo:method19() end function Foo:method20() end
	function Foo:method21() end function Foo:method22() end function Foo:method23() end
	function Foo:method24() end function Foo:method25() end function Foo:method26() end
	function Foo:method27() end function Foo:method28() end function Foo:method29() end

	local foo = Foo.new()
	local start = now()
	local i = 0
	while i < 500000 do
		foo:method0(); foo:method1(); foo:method2(); foo:method3(); foo:method4()
		foo:method5(); foo:method6(); foo:method7(); foo:method8(); foo:method9()
		foo:method10(); foo:method11(); foo:method12(); foo:method13(); foo:method14()
		foo:method15(); foo:method16(); foo:method17(); foo:method18(); foo:method19()
		foo:method20(); foo:method21(); foo:method22(); foo:method23(); foo:method24()
		foo:method25(); foo:method26(); foo:method27(); foo:method28(); foo:method29()
		i = i + 1
	end
	report("invocation", now() - start)
end

-- ---------- method_call ----------
local function bench_method_call()
	local Toggle = {}
	Toggle.__index = Toggle
	function Toggle.new(start_state)
		local self = setmetatable({}, Toggle)
		self.state = start_state
		return self
	end
	function Toggle:value() return self.state end
	function Toggle:activate()
		self.state = not self.state
		return self
	end

	local NthToggle = setmetatable({}, { __index = Toggle })
	NthToggle.__index = NthToggle
	function NthToggle.new(start_state, max_counter)
		local self = Toggle.new(start_state) -- super.init
		self.count_max = max_counter
		self.count = 0
		return setmetatable(self, NthToggle)
	end
	function NthToggle:activate()
		self.count = self.count + 1
		if self.count >= self.count_max then
			Toggle.activate(self) -- super.activate
			self.count = 0
		end
		return self
	end

	local start = now()
	local n = 100000
	local val = true
	local toggle = Toggle.new(val)

	for _ = 1, n do
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
		val = toggle:activate():value()
	end

	print(toggle:value())

	val = true
	local ntoggle = NthToggle.new(val, 3)

	for _ = 1, n do
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
		val = ntoggle:activate():value()
	end

	print(ntoggle:value())
	report("method_call", now() - start)
end

-- ---------- properties ----------
local function bench_properties()
	local Foo = {}
	Foo.__index = Foo
	function Foo.new()
		local self = setmetatable({}, Foo)
		self.field0 = 1; self.field1 = 1; self.field2 = 1; self.field3 = 1; self.field4 = 1
		self.field5 = 1; self.field6 = 1; self.field7 = 1; self.field8 = 1; self.field9 = 1
		self.field10 = 1; self.field11 = 1; self.field12 = 1; self.field13 = 1; self.field14 = 1
		self.field15 = 1; self.field16 = 1; self.field17 = 1; self.field18 = 1; self.field19 = 1
		self.field20 = 1; self.field21 = 1; self.field22 = 1; self.field23 = 1; self.field24 = 1
		self.field25 = 1; self.field26 = 1; self.field27 = 1; self.field28 = 1; self.field29 = 1
		return self
	end
	function Foo:method0() return self.field0 end
	function Foo:method1() return self.field1 end
	function Foo:method2() return self.field2 end
	function Foo:method3() return self.field3 end
	function Foo:method4() return self.field4 end
	function Foo:method5() return self.field5 end
	function Foo:method6() return self.field6 end
	function Foo:method7() return self.field7 end
	function Foo:method8() return self.field8 end
	function Foo:method9() return self.field9 end
	function Foo:method10() return self.field10 end
	function Foo:method11() return self.field11 end
	function Foo:method12() return self.field12 end
	function Foo:method13() return self.field13 end
	function Foo:method14() return self.field14 end
	function Foo:method15() return self.field15 end
	function Foo:method16() return self.field16 end
	function Foo:method17() return self.field17 end
	function Foo:method18() return self.field18 end
	function Foo:method19() return self.field19 end
	function Foo:method20() return self.field20 end
	function Foo:method21() return self.field21 end
	function Foo:method22() return self.field22 end
	function Foo:method23() return self.field23 end
	function Foo:method24() return self.field24 end
	function Foo:method25() return self.field25 end
	function Foo:method26() return self.field26 end
	function Foo:method27() return self.field27 end
	function Foo:method28() return self.field28 end
	function Foo:method29() return self.field29 end

	local foo = Foo.new()
	local start = now()
	local i = 0
	while i < 500000 do
		foo:method0(); foo:method1(); foo:method2(); foo:method3(); foo:method4()
		foo:method5(); foo:method6(); foo:method7(); foo:method8(); foo:method9()
		foo:method10(); foo:method11(); foo:method12(); foo:method13(); foo:method14()
		foo:method15(); foo:method16(); foo:method17(); foo:method18(); foo:method19()
		foo:method20(); foo:method21(); foo:method22(); foo:method23(); foo:method24()
		foo:method25(); foo:method26(); foo:method27(); foo:method28(); foo:method29()
		i = i + 1
	end
	report("properties", now() - start)
end

-- ---------- trees ----------
local function bench_trees()
	local Tree = {}
	Tree.__index = Tree

	function Tree.new(depth)
		local self = setmetatable({}, Tree)
		self.depth = depth
		if depth > 0 then
			self.a = Tree.new(depth - 1)
			self.b = Tree.new(depth - 1)
			self.c = Tree.new(depth - 1)
			self.d = Tree.new(depth - 1)
			self.e = Tree.new(depth - 1)
		end
		return self
	end

	function Tree:walk()
		if self.depth == 0 then return 0 end
		return self.depth
			+ self.a:walk()
			+ self.b:walk()
			+ self.c:walk()
			+ self.d:walk()
			+ self.e:walk()
	end

	local tree = Tree.new(8)
	local start = now()
	local i = 0
	while i < 100 do
		if tree:walk() ~= 122068 then print("Error") end
		i = i + 1
	end
	report("trees", now() - start)
end

-- ---------- zoo ----------
local function make_zoo()
	local Zoo = {}
	Zoo.__index = Zoo
	function Zoo.new()
		local self = setmetatable({}, Zoo)
		self.aarvark = 1
		self.baboon = 1
		self.cat = 1
		self.donkey = 1
		self.elephant = 1
		self.fox = 1
		return self
	end
	function Zoo:ant() return self.aarvark end
	function Zoo:banana() return self.baboon end
	function Zoo:tuna() return self.cat end
	function Zoo:hay() return self.donkey end
	function Zoo:grass() return self.elephant end
	function Zoo:mouse() return self.fox end
	return Zoo.new()
end

local function bench_zoo()
	local zoo = make_zoo()
	local sum = 0
	local start = now()
	while sum < 10000000 do
		sum = sum + zoo:ant()
			+ zoo:banana()
			+ zoo:tuna()
			+ zoo:hay()
			+ zoo:grass()
			+ zoo:mouse()
	end
	print(sum)
	report("zoo", now() - start)
end

local function bench_zoo_batch()
	local zoo = make_zoo()
	local sum = 0
	local start = now()
	local batch = 0
	while now() - start < 10 do
		local i = 0
		while i < 10000 do
			sum = sum + zoo:ant()
				+ zoo:banana()
				+ zoo:tuna()
				+ zoo:hay()
				+ zoo:grass()
				+ zoo:mouse()
			i = i + 1
		end
		batch = batch + 1
	end
	print("zoo_batch Time: " .. string.format("%.0f", (now() - start) * 1000))
	print("zoo_batch Sum: " .. sum)
	print("zoo_batch Batch: " .. batch)
	report("zoo_batch(10sec)", now() - start)
end

-- ---------- dispatch ----------
if which == "all" or which == "fib30" then bench_fib(30, 832040) end
if which == "all" or which == "fib35" then bench_fib(35, 9227465) end
if which == "all" or which == "fib40" then bench_fib(40) end
if which == "all" or which == "loop" then bench_loop() end
if which == "all" or which == "loopfor" then bench_loop_for() end
if which == "all" or which == "globalloop" then bench_global_loop() end
if which == "all" or which == "binary_trees" then bench_binary_trees() end
if which == "all" or which == "instantiation" then bench_instantiation() end
if which == "all" or which == "invocation" then bench_invocation() end
if which == "all" or which == "method_call" then bench_method_call() end
if which == "all" or which == "properties" then bench_properties() end
if which == "all" or which == "trees" then bench_trees() end
if which == "all" or which == "zoo" then bench_zoo() end
if which == "all" or which == "zoo_batch" then bench_zoo_batch() end
