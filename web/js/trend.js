// Temperature history per probe for the graphs. The grill's history is loaded once, after that every
// status poll adds a point, so the graphs stay current without extra requests. Kept in °C like the
// grill's history, converted to the display unit when drawn.
const Trend = (() => {
  const APPEND_EVERY_MS = 10000;   // the grill's own sample interval
  const series = {};               // probe_id -> [{t, c}], oldest first
  const connectedBefore = {};      // probe_id -> connected in the previous status
  const listeners = [];
  let loading = false;
  let started = false;

  const isNumber = (value) => typeof value === "number" && isFinite(value);

  // Value i of a tier was taken age + (n - 1 - i) * interval seconds before nowMs
  function tierPoints(tier, nowMs) {
    const count = tier.values.length;
    return tier.values.map((value, i) => ({
      t: nowMs - (tier.age + (count - 1 - i) * tier.interval) * 1000,
      c: value === null ? null : value / 10,
    }));
  }

  function fromResponse(response, nowMs) {
    const result = {};
    for (const probe of (response && response.probes) || []) {
      result[probe.probe_id] = tierPoints(probe.coarse, nowMs);
    }
    return result;
  }

  function append(points, celsius, nowMs) {
    const last = points[points.length - 1];
    if (last && nowMs - last.t < APPEND_EVERY_MS) return false;
    points.push({ t: nowMs, c: celsius });
    return true;
  }

  function toCelsius(value, unit) {
    if (!isNumber(value)) return null;
    return unit === "fahrenheit" ? (value - 32) / 1.8 : value;
  }

  function display(points, unit) {
    return points.map((point) => ({
      t: point.t,
      v: point.c === null ? null : unit === "fahrenheit" ? point.c * 1.8 + 32 : point.c,
    }));
  }

  function points(probeId, unit) {
    return display(series[probeId] || [], unit);
  }

  function onChange(listener) { listeners.push(listener); }

  async function load() {
    if (loading) return;
    loading = true;
    try {
      const fresh = fromResponse(await Api.get("/api/history", 8000), Date.now());
      Object.keys(series).forEach((id) => { delete series[id]; });
      Object.assign(series, fresh);
      listeners.forEach((listener) => listener());
    } catch (error) {
      // Keep what we have, the next reconnect or page show loads again
    } finally {
      loading = false;
    }
  }

  function onStatus(status) {
    let reload = !started;
    started = true;
    const now = Date.now();
    for (const probe of status.probes) {
      const id = probe.probe_id;
      if (probe.connected && !connectedBefore[id]) reload = true;
      connectedBefore[id] = probe.connected;
      if (!probe.connected) continue;
      if (!series[id]) series[id] = [];
      append(series[id], toCelsius(probe.temperature, status.temperature_unit), now);
    }
    if (reload) load();
  }

  if (typeof App !== "undefined") {
    App.onStatus(onStatus);
    document.addEventListener("visibilitychange", () => { if (!document.hidden) load(); });
  }

  return { tierPoints, fromResponse, append, toCelsius, display, points, onChange, load };
})();

if (typeof module === "object" && module.exports) module.exports = Trend;
