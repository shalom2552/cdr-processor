import { useCallback, useEffect, useRef, useState } from 'react'
import { api, type Subscriber } from '../lib/api'
import { canvasToPng, toJson } from '../lib/exports'
import { count, duration } from '../lib/format'
import { go, useLoad } from '../lib/hooks'
import { remember } from '../lib/history'
import { setSettings, useSettings } from '../lib/settings'
import { Failure, Loading, Panel, Picker, SearchIcon } from '../components/parts'
import { Suggest } from '../components/Suggest'

type GNode = { id: string; x: number; y: number; vx: number; vy: number; expanded: boolean; shown: number; total: number }
type GEdge = { a: string; b: string; duration: number; sms: number }

const key = (a: string, b: string) => (a < b ? `${a}|${b}` : `${b}|${a}`)

/* The contact graph around one subscriber. It knows only the edges it has fetched, so a
   node's size is the sum of its known weights, never its true one. */
export default function GraphScreen({ msisdn }: { msisdn?: string }) {
  const settings = useSettings()
  const canvas = useRef<HTMLCanvasElement | null>(null)
  const graph = useRef({ nodes: new Map<string, GNode>(), edges: new Map<string, GEdge>() })
  const view = useRef({ x: 0, y: 0, k: 1 })
  const drag = useRef<{ x: number; y: number } | null>(null)

  const [typed, setTyped] = useState(msisdn ?? '')
  const [version, setVersion] = useState(0)
  const [frozen, setFrozen] = useState(false)
  const [busy, setBusy] = useState(false)
  const [note, setNote] = useState('')
  const [selected, setSelected] = useState<string | null>(null)
  const [hovered, setHovered] = useState<GEdge | null>(null)

  const metric = settings.edgeMetric
  const weight = useCallback((edge: GEdge) => (metric === 'sms' ? edge.sms : edge.duration), [metric])

  const counters = useLoad<Subscriber | null>(
    (signal) => (selected ? api.subscriber(selected, signal) : Promise.resolve(null)),
    [selected],
  )

  const pull = useCallback(async (id: string, centre: boolean) => {
    const { nodes, edges } = graph.current
    if (!centre && nodes.size >= settings.maxNodes) {
      setNote(`the canvas is capped at ${settings.maxNodes} nodes. remove some, or raise the cap in settings.`)
      return
    }

    setBusy(true)
    setNote('')
    try {
      const answer = await api.peers(id, metric === 'sms' ? 'sms' : 'dur', settings.expandLimit, 0)
      const seed = nodes.get(id) ?? { id, x: 0, y: 0, vx: 0, vy: 0, expanded: false, shown: 0, total: 0 }
      seed.expanded = true
      seed.shown = answer.peers.length
      seed.total = answer.count
      nodes.set(id, seed)

      let refused = 0
      for (const peer of answer.peers) {
        if (!nodes.has(peer.msisdn)) {
          if (nodes.size >= settings.maxNodes) {
            refused += 1
            continue
          }
          const angle = Math.random() * Math.PI * 2
          nodes.set(peer.msisdn, {
            id: peer.msisdn,
            x: seed.x + Math.cos(angle) * 80,
            y: seed.y + Math.sin(angle) * 80,
            vx: 0, vy: 0, expanded: false, shown: 0, total: 0,
          })
        }
        edges.set(key(id, peer.msisdn), { a: id, b: peer.msisdn, duration: peer.duration, sms: peer.sms })
      }

      if (refused) setNote(`${refused} peers left off: the canvas is capped at ${settings.maxNodes} nodes.`)
      else if (answer.count > answer.peers.length) {
        setNote(`showing ${answer.peers.length} of ${count(answer.count)} peers of ${id}, heaviest first. the whole list is on its subscriber screen.`)
      }
      setVersion((n) => n + 1)
    } catch (failure: any) {
      setNote(failure?.status === 404 ? `${id} is in no link, so it has no peers.` : String(failure?.message ?? failure))
    } finally {
      setBusy(false)
    }
  }, [metric, settings.expandLimit, settings.maxNodes])

  useEffect(() => {
    if (!msisdn) return
    graph.current = { nodes: new Map(), edges: new Map() }
    view.current = { x: 0, y: 0, k: 1 }
    setSelected(msisdn)
    remember('graph', msisdn)
    pull(msisdn, true)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [msisdn])

  /* The wheel zooms the canvas and must not scroll the page with it, which a react
     onWheel cannot do: its listener is passive. */
  useEffect(() => {
    const element = canvas.current
    if (!element) return

    const zoom = (event: WheelEvent) => {
      event.preventDefault()
      const factor = event.deltaY < 0 ? 1.1 : 0.9
      view.current = { ...view.current, k: Math.min(4, Math.max(0.2, view.current.k * factor)) }
    }

    element.addEventListener('wheel', zoom, { passive: false })
    return () => element.removeEventListener('wheel', zoom)
  }, [msisdn])

  useEffect(() => {
    const element = canvas.current
    if (!element) return
    let running = true

    const step = () => {
      if (!running) return
      const { nodes, edges } = graph.current
      const context = element.getContext('2d')!
      const box = element.getBoundingClientRect()
      element.width = box.width * devicePixelRatio
      element.height = box.height * devicePixelRatio

      if (!frozen) {
        const list = [...nodes.values()]
        for (let i = 0; i < list.length; i += 1) {
          for (let j = i + 1; j < list.length; j += 1) {
            const one = list[i], other = list[j]
            let dx = other.x - one.x, dy = other.y - one.y
            let distance = Math.hypot(dx, dy) || 0.01
            if (distance > 400) continue
            const push = 900 / (distance * distance)
            dx /= distance; dy /= distance
            one.vx -= dx * push; one.vy -= dy * push
            other.vx += dx * push; other.vy += dy * push
          }
        }

        for (const edge of edges.values()) {
          const one = nodes.get(edge.a), other = nodes.get(edge.b)
          if (!one || !other) continue
          const dx = other.x - one.x, dy = other.y - one.y
          const distance = Math.hypot(dx, dy) || 0.01
          const pull = (distance - 90) * 0.008
          one.vx += (dx / distance) * pull; one.vy += (dy / distance) * pull
          other.vx -= (dx / distance) * pull; other.vy -= (dy / distance) * pull
        }

        for (const node of nodes.values()) {
          node.vx -= node.x * 0.002
          node.vy -= node.y * 0.002
          node.vx *= 0.85; node.vy *= 0.85
          node.x += node.vx; node.y += node.vy
        }
      }

      const style = getComputedStyle(element)
      const ink = style.getPropertyValue('--ink').trim() || '#ddd'
      const line = style.getPropertyValue('--edge').trim() || '#888'
      const mark = style.getPropertyValue('--accent').trim() || '#4fa'

      context.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0)
      context.clearRect(0, 0, box.width, box.height)
      context.translate(box.width / 2 + view.current.x, box.height / 2 + view.current.y)
      context.scale(view.current.k, view.current.k)

      const weights = [...edges.values()].map(weight)
      const heaviest = Math.max(1, ...weights)

      context.strokeStyle = line
      for (const edge of edges.values()) {
        const one = nodes.get(edge.a), other = nodes.get(edge.b)
        if (!one || !other) continue
        context.globalAlpha = edge === hovered ? 1 : 0.5
        context.lineWidth = 0.5 + (weight(edge) / heaviest) * 5
        context.beginPath()
        context.moveTo(one.x, one.y)
        context.lineTo(other.x, other.y)
        context.stroke()
      }
      context.globalAlpha = 1

      const known = new Map<string, number>()
      for (const edge of edges.values()) {
        known.set(edge.a, (known.get(edge.a) ?? 0) + weight(edge))
        known.set(edge.b, (known.get(edge.b) ?? 0) + weight(edge))
      }
      const biggest = Math.max(1, ...known.values())

      context.font = '11px system-ui, sans-serif'
      for (const node of nodes.values()) {
        const size = 4 + Math.sqrt((known.get(node.id) ?? 0) / biggest) * 14
        context.beginPath()
        context.arc(node.x, node.y, size, 0, Math.PI * 2)
        context.fillStyle = node.id === selected ? mark : node.expanded ? line : ink
        context.fill()
        if (node.total > node.shown && node.expanded) {
          context.strokeStyle = mark
          context.lineWidth = 1.5
          context.stroke()
        }
        if (nodes.size <= 80 || node.id === selected) {
          context.fillStyle = ink
          context.fillText(node.id, node.x + size + 3, node.y + 4)
        }
      }

      requestAnimationFrame(step)
    }

    requestAnimationFrame(step)
    return () => { running = false }
  }, [frozen, hovered, selected, version, weight])

  const at = (event: React.MouseEvent) => {
    const box = event.currentTarget.getBoundingClientRect()
    return {
      x: (event.clientX - box.left - box.width / 2 - view.current.x) / view.current.k,
      y: (event.clientY - box.top - box.height / 2 - view.current.y) / view.current.k,
    }
  }

  const nodeAt = (point: { x: number; y: number }) =>
    [...graph.current.nodes.values()].find((node) => Math.hypot(node.x - point.x, node.y - point.y) < 14)

  const edgeAt = (point: { x: number; y: number }) =>
    [...graph.current.edges.values()].find((edge) => {
      const one = graph.current.nodes.get(edge.a), other = graph.current.nodes.get(edge.b)
      if (!one || !other) return false
      const length = Math.hypot(other.x - one.x, other.y - one.y) || 1
      const away = Math.abs((other.x - one.x) * (one.y - point.y) - (one.x - point.x) * (other.y - one.y)) / length
      const along = ((point.x - one.x) * (other.x - one.x) + (point.y - one.y) * (other.y - one.y)) / (length * length)
      return away < 4 && along > 0 && along < 1
    })

  if (!msisdn) {
    return (
      <div className="screen">
        <Panel title="look up">
          <form className="entry" onSubmit={(event) => { event.preventDefault(); const id = typed.match(/\d+/g)?.[0]; if (id) go(`graph/${id}`) }}>
            <div className="entry-row">
              <input inputMode="numeric" placeholder="msisdn at the centre" value={typed}
                     onChange={(event) => setTyped(event.target.value)} />
              <button type="submit" aria-label="draw" title="draw"><SearchIcon /></button>
            </div>
          </form>
          <Suggest onPick={(id) => go(`graph/${id}`)} />
        </Panel>
      </div>
    )
  }

  const nodes = graph.current.nodes
  const chosen = selected ? nodes.get(selected) : undefined

  return (
    <div className="screen graph-screen">
      <div className="row-between graph-bar">
        <Picker options={['duration', 'sms'] as const} chosen={metric} onPick={(edgeMetric) => setSettings({ edgeMetric })} />
        <span className="quiet">
          edge thickness is {metric === 'sms' ? 'messages' : 'call seconds'}. node size is the sum of
          its <em>known</em> edges — the ones fetched, not all it has.
        </span>
        <div className="actions">
          <button onClick={() => setFrozen(!frozen)}>{frozen ? 'unfreeze' : 'freeze'}</button>
          <button onClick={() => { view.current = { x: 0, y: 0, k: 1 }; setVersion((n) => n + 1) }}>recentre</button>
          <button onClick={() => canvas.current && canvasToPng(canvas.current, `graph-${msisdn}.png`)}>png</button>
          <button onClick={() => toJson(`graph-${msisdn}.json`, {
            centre: msisdn,
            nodes: [...nodes.values()].map(({ id, expanded, shown, total }) => ({ id, expanded, shown, total })),
            edges: [...graph.current.edges.values()],
          })}>json</button>
        </div>
      </div>

      <div className="canvas-wrap">
        <canvas
          ref={canvas}
          onMouseDown={(event) => { drag.current = { x: event.clientX - view.current.x, y: event.clientY - view.current.y } }}
          onMouseUp={() => { drag.current = null }}
          onMouseLeave={() => { drag.current = null; setHovered(null) }}
          onMouseMove={(event) => {
            if (drag.current) {
              view.current = { ...view.current, x: event.clientX - drag.current.x, y: event.clientY - drag.current.y }
              return
            }
            const point = at(event)
            setHovered(nodeAt(point) ? null : edgeAt(point) ?? null)
          }}
          onClick={(event) => {
            const node = nodeAt(at(event))
            if (node) setSelected(node.id)
          }}
          onDoubleClick={(event) => {
            const node = nodeAt(at(event))
            if (node) pull(node.id, false)
          }}
        />
        {busy && <div className="canvas-note"><Loading what="peers" /></div>}
        {hovered && (
          <div className="canvas-note">
            {hovered.a} ↔ {hovered.b}: {duration(hovered.duration)}, {count(hovered.sms)} messages
          </div>
        )}
      </div>

      {note && <p className="banner">{note}</p>}
      <p className="quiet">
        {nodes.size} of {settings.maxNodes} nodes, {graph.current.edges.size} edges. click a node to
        select it, double-click to expand it by {settings.expandLimit} peers, drag to pan, wheel to zoom.
      </p>

      {chosen && (
        <Panel
          title={`selected ${chosen.id}`}
          right={
            <div className="actions">
              <button onClick={() => pull(chosen.id, false)}>expand</button>
              <button onClick={() => {
                graph.current.nodes.delete(chosen.id)
                for (const [id, edge] of graph.current.edges) {
                  if (edge.a === chosen.id || edge.b === chosen.id) graph.current.edges.delete(id)
                }
                setSelected(null)
                setVersion((n) => n + 1)
              }}>remove</button>
              <button className="primary" onClick={() => go(`subscriber/${chosen.id}`)}>open</button>
            </div>
          }
        >
          {chosen.expanded && chosen.total > chosen.shown && (
            <p className="quiet">showing {chosen.shown} of {count(chosen.total)} peers, heaviest first.</p>
          )}
          {counters.error && <Failure error={counters.error} what={chosen.id} />}
          {counters.data && (
            <table className="grid">
              <tbody>
                <tr><th>voice out</th><td>{duration(counters.data['voice-out'])}</td>
                    <th>voice in</th><td>{duration(counters.data['voice-in'])}</td></tr>
                <tr><th>sms out</th><td>{count(counters.data['sms-out'])}</td>
                    <th>sms in</th><td>{count(counters.data['sms-in'])}</td></tr>
                <tr><th>no answer</th><td>{count(counters.data['no-answer'])}</td>
                    <th>busy</th><td>{count(counters.data.busy)}</td></tr>
              </tbody>
            </table>
          )}
        </Panel>
      )}
    </div>
  )
}
