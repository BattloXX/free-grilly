// Geometry for the temperature graphs: points in, SVG path data out. No DOM access, so the unit
// tests can run it in Node. Points are {t: milliseconds, v: degrees or null for a gap}, oldest first.
const Graph = (() => {
  const MIN_SPAN = 5;      // degrees, so a flat line doesn't turn noise into mountains
  const PADDING = 0.08;    // share of the range kept free above and below

  const round = (value) => Math.round(value * 10) / 10;

  // Averages the points into at most `buckets` equal time slices. A slice with only gaps stays a gap.
  function downsample(points, buckets) {
    if (points.length <= buckets) return points;
    const start = points[0].t;
    const width = (points[points.length - 1].t - start) / buckets || 1;
    const result = [];
    let index = 0;
    for (let bucket = 0; bucket < buckets; bucket++) {
      const end = bucket === buckets - 1 ? Infinity : start + (bucket + 1) * width;
      let sum = 0, valid = 0, seen = 0, t = 0;
      while (index < points.length && points[index].t < end) {
        const point = points[index++];
        seen++;
        t = point.t;
        if (point.v !== null) { sum += point.v; valid++; }
      }
      if (seen > 0) result.push({ t, v: valid > 0 ? sum / valid : null });
    }
    return result;
  }

  // Path data for a width x height box, or null with fewer than 2 readings. The target (and the
  // range minimum) are part of the scale, so their guide lines are always visible.
  function layout(points, opts) {
    const values = points.filter((point) => point.v !== null).map((point) => point.v);
    if (values.length < 2) return null;

    let low = values[0];
    let high = values[0];
    for (const value of values) {
      if (value < low) low = value;
      if (value > high) high = value;
    }
    if (opts.mode !== "off") {
      high = Math.max(high, opts.target);
      low = Math.min(low, opts.target);
    }
    if (opts.mode === "range") low = Math.min(low, opts.minimum);
    if (high - low < MIN_SPAN) {
      const middle = (high + low) / 2;
      low = middle - MIN_SPAN / 2;
      high = middle + MIN_SPAN / 2;
    }
    const padding = (high - low) * PADDING;
    low -= padding;
    high += padding;

    const shown = downsample(points, opts.width);
    const first = shown[0].t;
    const span = shown[shown.length - 1].t - first || 1;
    const x = (t) => round((t - first) / span * opts.width);
    const y = (v) => round((high - v) / (high - low) * opts.height);

    // Split at gaps, each run of readings is its own line and area
    const runs = [];
    let run = [];
    for (const point of shown) {
      if (point.v === null) {
        if (run.length) runs.push(run);
        run = [];
      } else {
        run.push([x(point.t), y(point.v)]);
      }
    }
    if (run.length) runs.push(run);

    const line = runs.map((r) => "M" + r.map(([px, py]) => px + " " + py).join("L")).join("");
    const area = runs.filter((r) => r.length > 1).map((r) =>
      "M" + r[0][0] + " " + opts.height + "L" + r.map(([px, py]) => px + " " + py).join("L") +
      "L" + r[r.length - 1][0] + " " + opts.height + "Z").join("");

    return {
      line,
      area,
      target: opts.mode === "off" ? null : y(opts.target),
      minimum: opts.mode === "range" ? y(opts.minimum) : null,
    };
  }

  return { downsample, layout };
})();

if (typeof module === "object" && module.exports) module.exports = Graph;
