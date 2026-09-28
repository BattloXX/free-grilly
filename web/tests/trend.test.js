const test = require("node:test");
const assert = require("node:assert/strict");
const Trend = require("../js/trend.js");

test("tierPoints dates every value from age and interval", () => {
  const points = Trend.tierPoints({ interval: 60, age: 30, values: [200, null, 210] }, 1000000);
  assert.deepEqual(points, [
    { t: 850000, c: 20 },
    { t: 910000, c: null },
    { t: 970000, c: 21 },
  ]);
});

test("fromResponse uses the coarse tier per probe", () => {
  const response = { probes: [
    { probe_id: 2, coarse: { interval: 60, age: 0, values: [700] } },
    { probe_id: 5, coarse: { interval: 120, age: 0, values: [] } },
  ] };
  assert.deepEqual(Trend.fromResponse(response, 5000), { 2: [{ t: 5000, c: 70 }], 5: [] });
  assert.deepEqual(Trend.fromResponse(null, 5000), {});
});

test("append adds at most one point per 10 seconds", () => {
  const points = [];
  assert.equal(Trend.append(points, 20, 1000), true);
  assert.equal(Trend.append(points, 21, 10999), false);
  assert.equal(Trend.append(points, 22, 11000), true);
  assert.deepEqual(points, [{ t: 1000, c: 20 }, { t: 11000, c: 22 }]);
});

test("toCelsius converts fahrenheit and rejects non-numbers", () => {
  assert.equal(Trend.toCelsius(212, "fahrenheit"), 100);
  assert.equal(Trend.toCelsius(50, "celcius"), 50);
  assert.equal(Trend.toCelsius(NaN, "celcius"), null);
  assert.equal(Trend.toCelsius(null, "celcius"), null);
});

test("display converts to the display unit and keeps gaps", () => {
  const points = [{ t: 1, c: 100 }, { t: 2, c: null }];
  assert.deepEqual(Trend.display(points, "fahrenheit"), [{ t: 1, v: 212 }, { t: 2, v: null }]);
  assert.deepEqual(Trend.display(points, "celcius"), [{ t: 1, v: 100 }, { t: 2, v: null }]);
});

test("thin keeps the newest half and thins the oldest half", () => {
  const points = Array.from({ length: 10 }, (_, i) => ({ t: i, c: i }));
  Trend.thin(points, 8);
  assert.ok(points.length <= 8);
  assert.deepEqual(points.slice(-5), [{ t: 5, c: 5 }, { t: 6, c: 6 }, { t: 7, c: 7 }, { t: 8, c: 8 }, { t: 9, c: 9 }]);
  assert.deepEqual(points.slice(0, -5), [{ t: 0, c: 0 }, { t: 2, c: 2 }, { t: 4, c: 4 }]);
});

test("append caps the series length over a long cook", () => {
  const points = [];
  let t = 0;
  for (let i = 0; i < 5000; i++) {
    t += 10000;
    Trend.append(points, i, t);
  }
  assert.ok(points.length <= 4320);
});
