// Icon drawing commands copied directly from AppIcon.qml.
export function paintAppIcon(
  ctx: CanvasRenderingContext2D,
  name: string,
  size: number,
  color: string,
) {
  const w = size,
    h = size,
    midX = w / 2,
    midY = h / 2;
  ctx.clearRect(0, 0, size, size);
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = Math.max(1.15, size * 0.085);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  const line = (
    c: CanvasRenderingContext2D,
    x1: number,
    y1: number,
    x2: number,
    y2: number,
  ) => {
    c.beginPath();
    c.moveTo(x1, y1);
    c.lineTo(x2, y2);
    c.stroke();
  };
  const rect = (
    c: CanvasRenderingContext2D,
    x: number,
    y: number,
    a: number,
    b: number,
    fill: boolean,
  ) => {
    c.beginPath();
    c.rect(x, y, a, b);
    if (fill) c.fill();
    else c.stroke();
  };
  const circle = (
    c: CanvasRenderingContext2D,
    x: number,
    y: number,
    r: number,
    fill: boolean,
  ) => {
    c.beginPath();
    c.arc(x, y, r, 0, Math.PI * 2);
    if (fill) c.fill();
    else c.stroke();
  };
  const poly = (
    c: CanvasRenderingContext2D,
    pts: number[][],
    fill: boolean,
  ) => {
    c.beginPath();
    c.moveTo(pts[0][0], pts[0][1]);
    for (const p of pts.slice(1)) c.lineTo(p[0], p[1]);
    c.closePath();
    if (fill) c.fill();
    else c.stroke();
  };
  switch (name) {
    case "text":
      line(ctx, w * 0.2, h * 0.2, w * 0.8, h * 0.2);
      line(ctx, midX, h * 0.2, midX, h * 0.8);
      line(ctx, w * 0.32, h * 0.8, w * 0.68, h * 0.8);
      break;
    case "check":
      line(ctx, w * 0.18, midY, w * 0.42, h * 0.74);
      line(ctx, w * 0.42, h * 0.74, w * 0.82, h * 0.28);
      break;
    case "keyframe":
      poly(
        ctx,
        [
          [midX, h * 0.14],
          [w * 0.86, midY],
          [midX, h * 0.86],
          [w * 0.14, midY],
        ],
        false,
      );
      break;
    case "curve":
      ctx.beginPath();
      ctx.moveTo(w * 0.14, h * 0.78);
      ctx.bezierCurveTo(
        w * 0.34,
        h * 0.78,
        w * 0.36,
        h * 0.22,
        w * 0.86,
        h * 0.22,
      );
      ctx.stroke();
      circle(ctx, w * 0.14, h * 0.78, w * 0.055, true);
      circle(ctx, w * 0.86, h * 0.22, w * 0.055, true);
      break;
    case "search":
      circle(ctx, w * 0.44, h * 0.44, w * 0.22, false);
      line(ctx, w * 0.6, h * 0.6, w * 0.8, h * 0.8);
      break;
    case "adjust":
      line(ctx, w * 0.18, h * 0.28, w * 0.82, h * 0.28);
      line(ctx, w * 0.18, midY, w * 0.82, midY);
      line(ctx, w * 0.18, h * 0.72, w * 0.82, h * 0.72);
      circle(ctx, w * 0.34, h * 0.28, w * 0.06, true);
      circle(ctx, w * 0.62, midY, w * 0.06, true);
      circle(ctx, w * 0.46, h * 0.72, w * 0.06, true);
      break;
    case "layers":
      poly(
        ctx,
        [
          [midX, h * 0.15],
          [w * 0.84, h * 0.34],
          [midX, h * 0.53],
          [w * 0.16, h * 0.34],
        ],
        false,
      );
      ctx.beginPath();
      ctx.moveTo(w * 0.16, h * 0.51);
      ctx.lineTo(midX, h * 0.7);
      ctx.lineTo(w * 0.84, h * 0.51);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(w * 0.16, h * 0.68);
      ctx.lineTo(midX, h * 0.87);
      ctx.lineTo(w * 0.84, h * 0.68);
      ctx.stroke();
      break;
    case "carousel":
      rect(ctx, w * 0.31, h * 0.18, w * 0.38, h * 0.64, false);
      line(ctx, w * 0.16, h * 0.3, w * 0.16, h * 0.7);
      line(ctx, w * 0.84, h * 0.3, w * 0.84, h * 0.7);
      break;
    case "timeline":
      rect(ctx, w * 0.15, h * 0.3, w * 0.32, h * 0.18, false);
      rect(ctx, w * 0.33, h * 0.64, w * 0.52, h * 0.18, false);
      line(ctx, w * 0.64, h * 0.21, w * 0.64, h * 0.88);
      poly(
        ctx,
        [
          [w * 0.55, h * 0.12],
          [w * 0.73, h * 0.12],
          [w * 0.64, h * 0.24],
        ],
        true,
      );
      break;
    case "reset":
      ctx.beginPath();
      ctx.arc(midX, midY, w * 0.3, Math.PI * 0.15, Math.PI * 1.18, true);
      ctx.stroke();
      line(ctx, w * 0.24, h * 0.18, w * 0.24, h * 0.34);
      line(ctx, w * 0.24, h * 0.34, w * 0.42, h * 0.34);
      break;
    case "image":
      rect(ctx, w * 0.16, h * 0.2, w * 0.68, h * 0.6, false);
      circle(ctx, w * 0.37, h * 0.38, w * 0.055, true);
      line(ctx, w * 0.22, h * 0.68, w * 0.44, h * 0.48);
      line(ctx, w * 0.44, h * 0.48, w * 0.58, h * 0.61);
      line(ctx, w * 0.58, h * 0.61, w * 0.72, h * 0.43);
      break;
    case "visible":
      ctx.beginPath();
      ctx.moveTo(w * 0.14, midY);
      ctx.quadraticCurveTo(midX, h * 0.22, w * 0.86, midY);
      ctx.quadraticCurveTo(midX, h * 0.78, w * 0.14, midY);
      ctx.stroke();
      circle(ctx, midX, midY, w * 0.13, false);
      circle(ctx, midX, midY, w * 0.035, true);
      break;
  }
}
