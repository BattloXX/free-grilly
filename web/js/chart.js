// Large temperature chart for the probe editor. The svg stretches to the width, so the labels are
// HTML on top of it instead of svg text (which would stretch too).
const Chart = (() => {
  const WIDTH = 300;
  const HEIGHT = 140;

  function create(container) {
    container.innerHTML =
      '<div class="chart">' +
      '<svg class="chart-svg" viewBox="0 0 ' + WIDTH + ' ' + HEIGHT + '" preserveAspectRatio="none" aria-hidden="true">' +
      '<g class="chart-grid"></g><rect class="graph-band" x="0" width="' + WIDTH + '"/>' +
      '<path class="graph-area"/><path class="graph-line"/><line class="graph-target" x1="0" x2="' + WIDTH + '"/></svg>' +
      '<div class="chart-y" aria-hidden="true"></div>' +
      '<div class="chart-x" aria-hidden="true"><span class="chart-start"></span><span>now</span></div>' +
      '<div class="graph-empty">Collecting readings…</div>' +
      '</div>';
    const root = container.firstElementChild;
    const svg = root.querySelector("svg");
    const grid = svg.querySelector(".chart-grid");
    const yLabels = root.querySelector(".chart-y");
    const xLabels = root.querySelector(".chart-x");
    const empty = root.querySelector(".graph-empty");

    function update(points, opts) {
      root.dataset.status = opts.status;
      const result = Graph.layout(points, { width: WIDTH, height: HEIGHT, mode: opts.mode, target: opts.target, minimum: opts.minimum });
      svg.toggleAttribute("hidden", result === null);   // svg elements have no .hidden property
      yLabels.hidden = xLabels.hidden = result === null;
      empty.hidden = result !== null;
      if (result === null) return;

      svg.querySelector(".graph-line").setAttribute("d", result.line);
      svg.querySelector(".graph-area").setAttribute("d", result.area);
      const target = svg.querySelector(".graph-target");
      target.style.display = opts.mode === "target" ? "" : "none";
      if (opts.mode === "target") { target.setAttribute("y1", result.target); target.setAttribute("y2", result.target); }
      const band = svg.querySelector(".graph-band");
      band.style.display = opts.mode === "range" ? "" : "none";
      if (opts.mode === "range") {
        band.setAttribute("y", result.target);
        band.setAttribute("height", Math.max(0, result.minimum - result.target));
      }

      const y = (v) => (result.scaleHigh - v) / (result.scaleHigh - result.scaleLow) * HEIGHT;
      // Drop ticks that land within 8px of either edge, so their labels don't poke above the svg or
      // overlap the "…ago / now" row underneath it.
      const values = Graph.ticks(result.scaleLow, result.scaleHigh, 3)
        .filter((v) => y(v) > 8 && y(v) < HEIGHT - 8);
      grid.innerHTML = values.map((v) => '<line x1="0" x2="' + WIDTH + '" y1="' + y(v) + '" y2="' + y(v) + '"/>').join("");
      yLabels.innerHTML = values.map((v) =>
        '<span style="top:' + (y(v) / HEIGHT * 100) + '%">' + v + "°</span>").join("");
      root.querySelector(".chart-start").textContent = Format.ago((result.endT - result.startT) / 1000);
    }

    return { update };
  }

  return { create };
})();
