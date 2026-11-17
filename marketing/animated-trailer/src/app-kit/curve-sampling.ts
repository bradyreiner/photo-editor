// Port of ToneCurveEditor.qml::sampledCurvePoints. The native monotone Hermite
// tangent limiting is retained, including the 3.0 slope magnitude bound.
export type CurvePoint = { x: number; y: number };
export function sampleAppCurve(points: CurvePoint[]): CurvePoint[] {
  if (points.length < 2)
    return [
      { x: 0, y: 0 },
      { x: 1, y: 1 },
    ];
  const secants: number[] = [],
    tangents: number[] = [];
  for (let i = 0; i < points.length - 1; i++)
    secants.push(
      (points[i + 1].y - points[i].y) /
        Math.max(0.0001, points[i + 1].x - points[i].x),
    );
  tangents.push(secants[0]);
  for (let i = 1; i < points.length - 1; i++)
    tangents.push(
      secants[i - 1] * secants[i] <= 0
        ? 0
        : (secants[i - 1] + secants[i]) * 0.5,
    );
  tangents.push(secants[secants.length - 1]);
  for (let i = 0; i < secants.length; i++) {
    if (Math.abs(secants[i]) <= 0.000001) {
      tangents[i] = 0;
      tangents[i + 1] = 0;
      continue;
    }
    const a = tangents[i] / secants[i],
      b = tangents[i + 1] / secants[i],
      magnitude = Math.sqrt(a * a + b * b);
    if (magnitude > 3) {
      const scale = 3 / magnitude;
      tangents[i] = scale * a * secants[i];
      tangents[i + 1] = scale * b * secants[i];
    }
  }
  const samples: CurvePoint[] = [];
  for (let i = 0; i < points.length - 1; i++) {
    const start = points[i],
      end = points[i + 1],
      span = Math.max(0.0001, end.x - start.x),
      count = Math.max(8, Math.round(span * 120));
    for (let j = 0; j < count; j++) {
      const t = j / count,
        t2 = t * t,
        t3 = t2 * t;
      const y =
        (2 * t3 - 3 * t2 + 1) * start.y +
        (t3 - 2 * t2 + t) * span * tangents[i] +
        (-2 * t3 + 3 * t2) * end.y +
        (t3 - t2) * span * tangents[i + 1];
      samples.push({ x: start.x + span * t, y: Math.max(0, Math.min(1, y)) });
    }
  }
  samples.push(points[points.length - 1]);
  return samples;
}
