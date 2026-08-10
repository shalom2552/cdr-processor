import { useEffect, useRef, useState } from 'react'
import type { Unit } from '../lib/format'
import { clock, value as formatted } from '../lib/format'
import { svgToPng } from '../lib/exports'

export type Point = [number, number | null]

function useWidth<T extends HTMLElement>() {
  const ref = useRef<T | null>(null)
  const [width, setWidth] = useState(640)

  useEffect(() => {
    if (!ref.current) return
    const observer = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width))
    observer.observe(ref.current)
    return () => observer.disconnect()
  }, [])

  return { ref, width }
}

/* Ticks on round numbers, so an axis reads 0, 500, 1,000 and never 3,277. */
function ticksOf(high: number, howMany = 4): number[] {
  const rough = (high || 1) / howMany
  const magnitude = Math.pow(10, Math.floor(Math.log10(rough)))
  const step = ([1, 2, 2.5, 5, 10].find((one) => rough <= one * magnitude) ?? 10) * magnitude
  const top = Math.ceil((high || 1) / step) * step
  const ticks: number[] = []
  for (let value = 0; value <= top + step / 1000; value += step) ticks.push(value)
  return ticks.length > 1 ? ticks : [0, step]
}

export function LineChart({
  points,
  unit,
  label,
  height = 260,
  empty = 'no samples in this window yet',
}: {
  points: Point[]
  unit: Unit
  label: string
  height?: number
  empty?: string
}) {
  const { ref, width } = useWidth<HTMLDivElement>()
  const svg = useRef<SVGSVGElement | null>(null)
  const [hover, setHover] = useState<number | null>(null)

  const pad = { left: 78, right: 16, top: 16, bottom: 30 }
  const plotWidth = Math.max(10, width - pad.left - pad.right)
  const plotHeight = height - pad.top - pad.bottom

  const drawn = points.filter(([, v]) => v !== null) as [number, number][]
  const peak = Math.max(...drawn.map(([, v]) => v), 0)
  const ticks = ticksOf(peak)
  const top = ticks[ticks.length - 1]

  const first = drawn[0]?.[0] ?? 0
  const last = drawn[drawn.length - 1]?.[0] ?? first + 1
  const spanX = last === first ? 1 : last - first

  const x = (ts: number) => pad.left + ((ts - first) / spanX) * plotWidth
  const y = (v: number) => pad.top + plotHeight - (v / top) * plotHeight

  const line = drawn.map(([ts, v], index) => `${index ? 'L' : 'M'}${x(ts).toFixed(1)},${y(v).toFixed(1)}`).join(' ')
  const area = drawn.length
    ? `${line} L${x(last).toFixed(1)},${(pad.top + plotHeight).toFixed(1)} L${x(first).toFixed(1)},${(pad.top + plotHeight).toFixed(1)} Z`
    : ''

  const near = hover === null ? null : drawn.reduce<[number, number] | null>((best, point) => {
    if (!best) return point
    return Math.abs(x(point[0]) - hover) < Math.abs(x(best[0]) - hover) ? point : best
  }, null)

  return (
    <div className="chart" ref={ref}>
      <div className="chart-head">
        <span className="chart-label">{label}</span>
        <span className="chart-now">
          {near ? `${clock(near[0])} · ${formatted(near[1], unit)}` : drawn.length ? formatted(drawn[drawn.length - 1][1], unit) : ''}
        </span>
        <button className="ghost" onClick={() => svg.current && svgToPng(svg.current, `${label}.png`)}>
          png
        </button>
      </div>

      {drawn.length === 0 ? (
        <div className="chart-empty" style={{ height }}>{empty}</div>
      ) : (
        <svg
          ref={svg}
          width={width}
          height={height}
          onMouseMove={(event) => setHover(event.clientX - event.currentTarget.getBoundingClientRect().left)}
          onMouseLeave={() => setHover(null)}
        >
          {ticks.map((tick) => (
            <g key={tick}>
              <line className="grid" x1={pad.left} x2={width - pad.right} y1={y(tick)} y2={y(tick)} />
              <text className="axis" x={pad.left - 10} y={y(tick) + 4} textAnchor="end">
                {formatted(tick, unit)}
              </text>
            </g>
          ))}

          {[first, first + spanX / 2, last].map((ts, index) => (
            <text
              key={ts}
              className="axis"
              x={x(ts)}
              y={height - 8}
              textAnchor={index === 0 ? 'start' : index === 2 ? 'end' : 'middle'}
            >
              {clock(ts)}
            </text>
          ))}

          <path className="area" d={area} />
          <path className="line" d={line} />
          {near && (
            <g>
              <line className="crosshair" x1={x(near[0])} x2={x(near[0])} y1={pad.top} y2={pad.top + plotHeight} />
              <circle className="dot" cx={x(near[0])} cy={y(near[1])} r={4} />
            </g>
          )}
        </svg>
      )}
    </div>
  )
}

export function Sparkline({ points, height = 40 }: { points: Point[]; height?: number }) {
  const drawn = points.filter(([, v]) => v !== null) as [number, number][]
  if (drawn.length < 2) return <div className="spark-empty">no rate yet</div>

  const width = 240
  const peak = Math.max(...drawn.map(([, v]) => v), 1)
  const first = drawn[0][0]
  const span = drawn[drawn.length - 1][0] - first || 1
  const at = ([ts, v]: [number, number]) => [((ts - first) / span) * width, height - (v / peak) * (height - 4) - 2]
  const line = drawn.map((point, index) => `${index ? 'L' : 'M'}${at(point).map((n) => n.toFixed(1)).join(',')}`).join(' ')

  return (
    <svg className="spark" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" height={height}>
      <path className="area" d={`${line} L${width},${height} L0,${height} Z`} />
      <path className="line" d={line} />
    </svg>
  )
}

export type Slice = { label: string; value: number }

export function Donut({ slices, unit, size = 200 }: { slices: Slice[]; unit: Unit; size?: number }) {
  const total = slices.reduce((sum, slice) => sum + slice.value, 0)
  if (!total) return <div className="chart-empty">nothing counted yet</div>

  const radius = size / 2
  const thickness = size / 4
  let angle = -Math.PI / 2

  const arcs = slices.map((slice, index) => {
    const sweep = (slice.value / total) * Math.PI * 2
    const from = angle
    angle += sweep
    const point = (a: number, r: number) => [radius + r * Math.cos(a), radius + r * Math.sin(a)]
    const [x1, y1] = point(from, radius - 2)
    const [x2, y2] = point(angle, radius - 2)
    const [x3, y3] = point(angle, radius - thickness)
    const [x4, y4] = point(from, radius - thickness)
    const large = sweep > Math.PI ? 1 : 0
    return (
      <path
        key={slice.label}
        className={`slice c${index % 8}`}
        d={`M${x1},${y1} A${radius - 2},${radius - 2} 0 ${large} 1 ${x2},${y2} L${x3},${y3} A${radius - thickness},${radius - thickness} 0 ${large} 0 ${x4},${y4} Z`}
      >
        <title>{`${slice.label}: ${formatted(slice.value, unit)}`}</title>
      </path>
    )
  })

  return (
    <div className="donut">
      <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}>{arcs}</svg>
      <ul className="legend">
        {slices.map((slice, index) => (
          <li key={slice.label}>
            <span className={`swatch c${index % 8}`} />
            <span className="legend-label">{slice.label}</span>
            <span className="legend-value">{formatted(slice.value, unit)}</span>
            <span className="legend-share">{((slice.value / total) * 100).toFixed(1)}%</span>
          </li>
        ))}
      </ul>
    </div>
  )
}

export type Band = { label: string; points: Point[] }

export function StackedArea({ bands, height = 240 }: { bands: Band[]; height?: number }) {
  const { ref, width } = useWidth<HTMLDivElement>()
  const stamps = bands[0]?.points.map(([ts]) => ts) ?? []

  /* Shares per stamp. A stamp where nothing moved has no mix of its own, so it carries the
     last one forward instead of collapsing the stack to zero. */
  const columns: (number[] | null)[] = stamps.map((_, index) => {
    const values = bands.map((band) => band.points[index]?.[1] ?? 0)
    const total = values.reduce((sum, one) => sum + one, 0)
    return total > 0 ? values.map((one) => one / total) : null
  })

  let carried: number[] | null = null
  const shares = columns.map((column) => (carried = column ?? carried))
  const known = shares.findIndex((column) => column !== null)

  const pad = { left: 12, right: 12, top: 12, bottom: 26 }
  const plotWidth = Math.max(10, width - pad.left - pad.right)
  const plotHeight = height - pad.top - pad.bottom
  const first = stamps[known] ?? 0
  const span = (stamps[stamps.length - 1] ?? first + 1) - first || 1

  const running = stamps.map(() => 0)
  const shapes = bands.map((band, order) => {
    const top: string[] = []
    const bottom: string[] = []
    stamps.forEach((ts, index) => {
      if (known === -1 || index < known) return
      const x = pad.left + ((ts - first) / span) * plotWidth
      const base = running[index]
      const next = base + (shares[index]![order] ?? 0)
      running[index] = next
      top.push(`${x.toFixed(1)},${(pad.top + plotHeight - next * plotHeight).toFixed(1)}`)
      bottom.unshift(`${x.toFixed(1)},${(pad.top + plotHeight - base * plotHeight).toFixed(1)}`)
    })
    return <path key={band.label} className={`band c${order % 8}`} d={`M${top.join('L')}L${bottom.join('L')}Z`} />
  })

  return (
    <div className="chart" ref={ref}>
      {stamps.length < 2 || known === -1 ? (
        <div className="chart-empty" style={{ height }}>nothing moved in this window</div>
      ) : (
        <svg width={width} height={height}>
          {shapes}
          <text className="axis" x={pad.left} y={height - 8}>{clock(first)}</text>
          <text className="axis" x={width - pad.right} y={height - 8} textAnchor="end">
            {clock(stamps[stamps.length - 1])}
          </text>
        </svg>
      )}
      <ul className="legend wrap">
        {bands.map((band, order) => (
          <li key={band.label}>
            <span className={`swatch c${order % 8}`} />
            <span className="legend-label">{band.label}</span>
          </li>
        ))}
      </ul>
    </div>
  )
}

export function PairedBars({
  rows,
  unit,
}: {
  rows: { label: string; out: number; in: number }[]
  unit: Unit
}) {
  const widest = Math.max(1, ...rows.map((row) => Math.max(row.out, row.in)))
  return (
    <table className="bars">
      <tbody>
        {rows.map((row) => (
          <tr key={row.label}>
            <th>{row.label}</th>
            <td>
              <div className="bar out" style={{ width: `${(row.out / widest) * 100}%` }} />
              <span>out {formatted(row.out, unit)}</span>
            </td>
            <td>
              <div className="bar in" style={{ width: `${(row.in / widest) * 100}%` }} />
              <span>in {formatted(row.in, unit)}</span>
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}
