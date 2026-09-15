// Node.js --jitless equivalents of the README benchmarks
'use strict';

function bench(name, fn) {
  const start = process.hrtime.bigint();
  fn();
  const ms = Number(process.hrtime.bigint() - start) / 1e6;
  console.log(`${name}: ${ms.toFixed(0)}ms`);
}

const which = process.argv[2] || "all";

// fib
function makeFib() {
  function fib(n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
  }
  return fib;
}
function benchFib(n, expected) {
  const fib = makeFib();
  const start = process.hrtime.bigint();
  const r = fib(n);
  const ms = Number(process.hrtime.bigint() - start) / 1e6;
  if (expected !== undefined) console.log(r === expected);
  console.log(`fib${n}: ${ms.toFixed(0)}ms`);
}

// loop 1e8
function benchLoop() {
  let i = 0;
  while (i < 100000000) i += 1;
  return i;
}
function benchGlobalLoop() {
  // match LoxFlux semantics: global counter incremented in a bare for loop
  globalThis.i = 0;
  for (i = 0; i < 100000000; ) {
    i += 1;
  }
}

// binary_trees
class Tree {
  constructor(item, depth) {
    this.item = item;
    if (depth > 0) {
      const item2 = item + item;
      depth -= 1;
      this.left = new Tree(item2 - 1, depth);
      this.right = new Tree(item2, depth);
    } else {
      this.left = null;
      this.right = null;
    }
  }
  check() {
    if (this.left === null) return this.item;
    return this.item + this.left.check() - this.right.check();
  }
}
function benchBinaryTrees() {
  const minDepth = 4, maxDepth = 14, stretchDepth = maxDepth + 1;
  new Tree(0, stretchDepth).check();
  const longLived = new Tree(0, maxDepth);
  let iterations = 2 ** maxDepth;
  let depth = minDepth;
  while (depth < stretchDepth) {
    let check = 0, i = 1;
    while (i <= iterations) {
      check += new Tree(i, depth).check() + new Tree(-i, depth).check();
      i += 1;
    }
    iterations /= 4;
    depth += 2;
  }
  longLived.check();
}

// instantiation
class Foo { constructor() {} }
function benchInstantiation() {
  let i = 0;
  while (i < 500000) {
    new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo();
    new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo();
    new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo(); new Foo();
    i += 1;
  }
}

// invocation
class Inv {
  method0() {} method1() {} method2() {} method3() {} method4() {}
  method5() {} method6() {} method7() {} method8() {} method9() {}
  method10() {} method11() {} method12() {} method13() {} method14() {}
  method15() {} method16() {} method17() {} method18() {} method19() {}
  method20() {} method21() {} method22() {} method23() {} method24() {}
  method25() {} method26() {} method27() {} method28() {} method29() {}
}
function benchInvocation() {
  const foo = new Inv();
  let i = 0;
  while (i < 500000) {
    foo.method0(); foo.method1(); foo.method2(); foo.method3(); foo.method4();
    foo.method5(); foo.method6(); foo.method7(); foo.method8(); foo.method9();
    foo.method10(); foo.method11(); foo.method12(); foo.method13(); foo.method14();
    foo.method15(); foo.method16(); foo.method17(); foo.method18(); foo.method19();
    foo.method20(); foo.method21(); foo.method22(); foo.method23(); foo.method24();
    foo.method25(); foo.method26(); foo.method27(); foo.method28(); foo.method29();
    i += 1;
  }
}

// method_call
class Toggle {
  constructor(startState) { this.state = startState; }
  value() { return this.state; }
  activate() { this.state = !this.state; return this; }
}
class NthToggle extends Toggle {
  constructor(startState, maxCounter) {
    super(startState);
    this.countMax = maxCounter;
    this.count = 0;
  }
  activate() {
    this.count += 1;
    if (this.count >= this.countMax) {
      super.activate();
      this.count = 0;
    }
    return this;
  }
}
function benchMethodCall() {
  const n = 100000;
  let toggle = new Toggle(true);
  for (let i = 0; i < n; i++) {
    toggle.activate().value(); toggle.activate().value(); toggle.activate().value();
    toggle.activate().value(); toggle.activate().value(); toggle.activate().value();
    toggle.activate().value(); toggle.activate().value(); toggle.activate().value();
    toggle.activate().value();
  }
  console.log(toggle.value());
  const ntoggle = new NthToggle(true, 3);
  for (let i = 0; i < n; i++) {
    ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value();
    ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value();
    ntoggle.activate().value(); ntoggle.activate().value(); ntoggle.activate().value();
    ntoggle.activate().value();
  }
  console.log(ntoggle.value());
}

// properties
class Props {
  constructor() {
    this.field0 = 1; this.field1 = 1; this.field2 = 1; this.field3 = 1; this.field4 = 1;
    this.field5 = 1; this.field6 = 1; this.field7 = 1; this.field8 = 1; this.field9 = 1;
    this.field10 = 1; this.field11 = 1; this.field12 = 1; this.field13 = 1; this.field14 = 1;
    this.field15 = 1; this.field16 = 1; this.field17 = 1; this.field18 = 1; this.field19 = 1;
    this.field20 = 1; this.field21 = 1; this.field22 = 1; this.field23 = 1; this.field24 = 1;
    this.field25 = 1; this.field26 = 1; this.field27 = 1; this.field28 = 1; this.field29 = 1;
  }
  method0() { return this.field0; } method1() { return this.field1; }
  method2() { return this.field2; } method3() { return this.field3; }
  method4() { return this.field4; } method5() { return this.field5; }
  method6() { return this.field6; } method7() { return this.field7; }
  method8() { return this.field8; } method9() { return this.field9; }
  method10() { return this.field10; } method11() { return this.field11; }
  method12() { return this.field12; } method13() { return this.field13; }
  method14() { return this.field14; } method15() { return this.field15; }
  method16() { return this.field16; } method17() { return this.field17; }
  method18() { return this.field18; } method19() { return this.field19; }
  method20() { return this.field20; } method21() { return this.field21; }
  method22() { return this.field22; } method23() { return this.field23; }
  method24() { return this.field24; } method25() { return this.field25; }
  method26() { return this.field26; } method27() { return this.field27; }
  method28() { return this.field28; } method29() { return this.field29; }
}
function benchProperties() {
  const foo = new Props();
  let i = 0;
  while (i < 500000) {
    foo.method0(); foo.method1(); foo.method2(); foo.method3(); foo.method4();
    foo.method5(); foo.method6(); foo.method7(); foo.method8(); foo.method9();
    foo.method10(); foo.method11(); foo.method12(); foo.method13(); foo.method14();
    foo.method15(); foo.method16(); foo.method17(); foo.method18(); foo.method19();
    foo.method20(); foo.method21(); foo.method22(); foo.method23(); foo.method24();
    foo.method25(); foo.method26(); foo.method27(); foo.method28(); foo.method29();
    i += 1;
  }
}

// trees
class Tree5 {
  constructor(depth) {
    this.depth = depth;
    if (depth > 0) {
      this.a = new Tree5(depth - 1);
      this.b = new Tree5(depth - 1);
      this.c = new Tree5(depth - 1);
      this.d = new Tree5(depth - 1);
      this.e = new Tree5(depth - 1);
    }
  }
  walk() {
    if (this.depth === 0) return 0;
    return this.depth + this.a.walk() + this.b.walk() + this.c.walk() + this.d.walk() + this.e.walk();
  }
}
function benchTrees() {
  const tree = new Tree5(8);
  for (let i = 0; i < 100; i++) {
    if (tree.walk() !== 122068) console.log("Error");
  }
}

// zoo
class Zoo {
  constructor() {
    this.aarvark = 1; this.baboon = 1; this.cat = 1;
    this.donkey = 1; this.elephant = 1; this.fox = 1;
  }
  ant() { return this.aarvark; }
  banana() { return this.baboon; }
  tuna() { return this.cat; }
  hay() { return this.donkey; }
  grass() { return this.elephant; }
  mouse() { return this.fox; }
}
function benchZoo() {
  const zoo = new Zoo();
  let sum = 0;
  while (sum < 10000000) {
    sum += zoo.ant() + zoo.banana() + zoo.tuna() + zoo.hay() + zoo.grass() + zoo.mouse();
  }
  console.log(sum);
}
function benchZooBatch() {
  const zoo = new Zoo();
  let sum = 0;
  const start = process.hrtime.bigint();
  let batch = 0;
  while (Number(process.hrtime.bigint() - start) / 1e9 < 10) {
    for (let i = 0; i < 10000; i++) {
      sum += zoo.ant() + zoo.banana() + zoo.tuna() + zoo.hay() + zoo.grass() + zoo.mouse();
    }
    batch += 1;
  }
  console.log(sum);
  console.log(`${batch}batch`);
}

const has = (k) => which === "all" || which === k;
if (has("fib30")) benchFib(30, 832040);
if (has("fib35")) benchFib(35, 9227465);
if (has("fib40")) benchFib(40);
if (has("loop")) bench("loop 1e8", benchLoop);
if (has("globalloop")) bench("global loop 1e8", benchGlobalLoop);
if (has("binary_trees")) bench("binary_trees", benchBinaryTrees);
if (has("instantiation")) bench("instantiation", benchInstantiation);
if (has("invocation")) bench("invocation", benchInvocation);
if (has("method_call")) bench("method_call", benchMethodCall);
if (has("properties")) bench("properties", benchProperties);
if (has("trees")) bench("trees", benchTrees);
if (has("zoo")) bench("zoo", benchZoo);
if (has("zoo_batch")) bench("zoo_batch(10sec)", benchZooBatch);
