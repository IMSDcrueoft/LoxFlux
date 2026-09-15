# Python equivalents of the README benchmarks (matches scripts/benchmark/*.lox semantics)
import time, sys

def now():
    return time.perf_counter()

def report(name, elapsed_s):
    print(f"{name}: {elapsed_s * 1000:.0f}ms")

# ---------- fib ----------
def make_fib():
    sys.setrecursionlimit(10000)
    def fib(n):
        if n < 2:
            return n
        return fib(n - 2) + fib(n - 1)
    return fib

def bench_fib(n, expected=None):
    fib = make_fib()
    start = now()
    r = fib(n)
    elapsed = now() - start
    if expected is not None:
        print(r == expected)
    report(f"fib{n}", elapsed)

# ---------- loop 1e8 ----------
def bench_loop():
    start = now()
    i = 0
    while i < 100000000:
        i += 1
    report("loop 1e8", now() - start)

def bench_global_loop():
    global i
    start = now()
    i = 0
    while i < 100000000:
        i += 1
    report("global loop 1e8", now() - start)

# ---------- binary_trees (lox port, maxDepth 14) ----------
def bench_binary_trees():
    class Tree:
        __slots__ = ("item", "depth", "left", "right")
        def __init__(self, item, depth):
            self.item = item
            self.depth = depth
            if depth > 0:
                item2 = item + item
                depth -= 1
                self.left = Tree(item2 - 1, depth)
                self.right = Tree(item2, depth)
            else:
                self.left = None
                self.right = None
        def check(self):
            if self.left is None:
                return self.item
            return self.item + self.left.check() - self.right.check()

    min_depth = 4
    max_depth = 14
    stretch_depth = max_depth + 1
    start = now()

    Tree(0, stretch_depth).check()
    long_lived = Tree(0, max_depth)
    iterations = 2 ** max_depth
    depth = min_depth
    while depth < stretch_depth:
        check = 0
        i = 1
        while i <= iterations:
            check += Tree(i, depth).check() + Tree(-i, depth).check()
            i += 1
        iterations //= 4
        depth += 2
    long_lived.check()
    report("binary_trees", now() - start)

# ---------- instantiation ----------
def bench_instantiation():
    class Foo:
        def __init__(self):
            pass
    start = now()
    i = 0
    while i < 500000:
        Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo()
        Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo()
        Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo(); Foo()
        i += 1
    report("instantiation", now() - start)

# ---------- invocation ----------
def bench_invocation():
    class Foo:
        def method0(self): pass
        def method1(self): pass
        def method2(self): pass
        def method3(self): pass
        def method4(self): pass
        def method5(self): pass
        def method6(self): pass
        def method7(self): pass
        def method8(self): pass
        def method9(self): pass
        def method10(self): pass
        def method11(self): pass
        def method12(self): pass
        def method13(self): pass
        def method14(self): pass
        def method15(self): pass
        def method16(self): pass
        def method17(self): pass
        def method18(self): pass
        def method19(self): pass
        def method20(self): pass
        def method21(self): pass
        def method22(self): pass
        def method23(self): pass
        def method24(self): pass
        def method25(self): pass
        def method26(self): pass
        def method27(self): pass
        def method28(self): pass
        def method29(self): pass
    foo = Foo()
    start = now()
    i = 0
    while i < 500000:
        foo.method0(); foo.method1(); foo.method2(); foo.method3(); foo.method4()
        foo.method5(); foo.method6(); foo.method7(); foo.method8(); foo.method9()
        foo.method10(); foo.method11(); foo.method12(); foo.method13(); foo.method14()
        foo.method15(); foo.method16(); foo.method17(); foo.method18(); foo.method19()
        foo.method20(); foo.method21(); foo.method22(); foo.method23(); foo.method24()
        foo.method25(); foo.method26(); foo.method27(); foo.method28(); foo.method29()
        i += 1
    report("invocation", now() - start)

# ---------- method_call ----------
def bench_method_call():
    class Toggle:
        def __init__(self, start_state):
            self.state = start_state
        def value(self):
            return self.state
        def activate(self):
            self.state = not self.state
            return self

    class NthToggle(Toggle):
        def __init__(self, start_state, max_counter):
            super().__init__(start_state)
            self.count_max = max_counter
            self.count = 0
        def activate(self):
            self.count += 1
            if self.count >= self.count_max:
                super().activate()
                self.count = 0
            return self

    start = now()
    n = 100000
    toggle = Toggle(True)
    for _ in range(n):
        toggle.activate().value(); toggle.activate().value(); toggle.activate().value()
        toggle.activate().value(); toggle.activate().value(); toggle.activate().value()
        toggle.activate().value(); toggle.activate().value(); toggle.activate().value()
        toggle.activate().value()
    print(toggle.value())

    ntoggle = NthToggle(True, 3)
    for _ in range(n):
        ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value()
        ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value()
        ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value()
        ntoggle.activate().value()
    print(ntoggle.value())
    report("method_call", now() - start)

# ---------- properties ----------
def bench_properties():
    class Foo:
        def __init__(self):
            self.field0 = 1; self.field1 = 1; self.field2 = 1; self.field3 = 1; self.field4 = 1
            self.field5 = 1; self.field6 = 1; self.field7 = 1; self.field8 = 1; self.field9 = 1
            self.field10 = 1; self.field11 = 1; self.field12 = 1; self.field13 = 1; self.field14 = 1
            self.field15 = 1; self.field16 = 1; self.field17 = 1; self.field18 = 1; self.field19 = 1
            self.field20 = 1; self.field21 = 1; self.field22 = 1; self.field23 = 1; self.field24 = 1
            self.field25 = 1; self.field26 = 1; self.field27 = 1; self.field28 = 1; self.field29 = 1
        def method0(self): return self.field0
        def method1(self): return self.field1
        def method2(self): return self.field2
        def method3(self): return self.field3
        def method4(self): return self.field4
        def method5(self): return self.field5
        def method6(self): return self.field6
        def method7(self): return self.field7
        def method8(self): return self.field8
        def method9(self): return self.field9
        def method10(self): return self.field10
        def method11(self): return self.field11
        def method12(self): return self.field12
        def method13(self): return self.field13
        def method14(self): return self.field14
        def method15(self): return self.field15
        def method16(self): return self.field16
        def method17(self): return self.field17
        def method18(self): return self.field18
        def method19(self): return self.field19
        def method20(self): return self.field20
        def method21(self): return self.field21
        def method22(self): return self.field22
        def method23(self): return self.field23
        def method24(self): return self.field24
        def method25(self): return self.field25
        def method26(self): return self.field26
        def method27(self): return self.field27
        def method28(self): return self.field28
        def method29(self): return self.field29
    foo = Foo()
    start = now()
    i = 0
    while i < 500000:
        foo.method0(); foo.method1(); foo.method2(); foo.method3(); foo.method4()
        foo.method5(); foo.method6(); foo.method7(); foo.method8(); foo.method9()
        foo.method10(); foo.method11(); foo.method12(); foo.method13(); foo.method14()
        foo.method15(); foo.method16(); foo.method17(); foo.method18(); foo.method19()
        foo.method20(); foo.method21(); foo.method22(); foo.method23(); foo.method24()
        foo.method25(); foo.method26(); foo.method27(); foo.method28(); foo.method29()
        i += 1
    report("properties", now() - start)

# ---------- trees ----------
def bench_trees():
    class Tree:
        __slots__ = ("depth", "a", "b", "c", "d", "e")
        def __init__(self, depth):
            self.depth = depth
            if depth > 0:
                self.a = Tree(depth - 1)
                self.b = Tree(depth - 1)
                self.c = Tree(depth - 1)
                self.d = Tree(depth - 1)
                self.e = Tree(depth - 1)
        def walk(self):
            if self.depth == 0:
                return 0
            return (self.depth + self.a.walk() + self.b.walk()
                    + self.c.walk() + self.d.walk() + self.e.walk())
    tree = Tree(8)
    start = now()
    for _ in range(100):
        if tree.walk() != 122068:
            print("Error")
    report("trees", now() - start)

# ---------- zoo ----------
def bench_zoo():
    class Zoo:
        def __init__(self):
            self.aarvark = 1
            self.baboon = 1
            self.cat = 1
            self.donkey = 1
            self.elephant = 1
            self.fox = 1
        def ant(self): return self.aarvark
        def banana(self): return self.baboon
        def tuna(self): return self.cat
        def hay(self): return self.donkey
        def grass(self): return self.elephant
        def mouse(self): return self.fox
    zoo = Zoo()
    sum_ = 0
    start = now()
    while sum_ < 10000000:
        sum_ += zoo.ant() + zoo.banana() + zoo.tuna() + zoo.hay() + zoo.grass() + zoo.mouse()
    print(sum_)
    report("zoo", now() - start)

def bench_zoo_batch():
    class Zoo:
        def __init__(self):
            self.aarvark = 1
            self.baboon = 1
            self.cat = 1
            self.donkey = 1
            self.elephant = 1
            self.fox = 1
        def ant(self): return self.aarvark
        def banana(self): return self.baboon
        def tuna(self): return self.cat
        def hay(self): return self.donkey
        def grass(self): return self.elephant
        def mouse(self): return self.fox
    zoo = Zoo()
    sum_ = 0
    start = now()
    batch = 0
    while now() - start < 10:
        for _ in range(10000):
            sum_ += zoo.ant() + zoo.banana() + zoo.tuna() + zoo.hay() + zoo.grass() + zoo.mouse()
        batch += 1
    print(sum_)
    print(batch)
    report("zoo_batch(10sec)", now() - start)

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("all", "fib30"): bench_fib(30, 832040)
    if which in ("all", "fib35"): bench_fib(35, 9227465)
    if which in ("all", "fib40"): bench_fib(40)
    if which in ("all", "loop"): bench_loop()
    if which in ("all", "globalloop"): bench_global_loop()
    if which in ("all", "binary_trees"): bench_binary_trees()
    if which in ("all", "instantiation"): bench_instantiation()
    if which in ("all", "invocation"): bench_invocation()
    if which in ("all", "method_call"): bench_method_call()
    if which in ("all", "properties"): bench_properties()
    if which in ("all", "trees"): bench_trees()
    if which in ("all", "zoo"): bench_zoo()
    if which in ("all", "zoo_batch"): bench_zoo_batch()
