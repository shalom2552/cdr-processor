import type { ReactNode } from 'react'
import { ApiError } from '../lib/api'
import { ago } from '../lib/format'
import { Sparkline, type Point } from './charts'

export function Tile({
  label,
  value,
  note,
  spark,
  delta,
}: {
  label: string
  value: string
  note?: string
  spark?: Point[]
  delta?: string
}) {
  return (
    <div className="tile">
      <div className="tile-label">{label}</div>
      <div className="tile-value">{value}</div>
      {note && <div className="tile-note">{note}</div>}
      {spark && (
        <div className="tile-spark">
          <Sparkline points={spark} />
          {delta && <span className="tile-delta">{delta}</span>}
        </div>
      )}
    </div>
  )
}

export function Panel({ title, right, children }: { title: string; right?: ReactNode; children: ReactNode }) {
  return (
    <section className="panel">
      <header>
        <h2>{title}</h2>
        {right}
      </header>
      {children}
    </section>
  )
}

/* The three failures the ui must never render alike: gateway down, store down, not found. */
export function Failure({ error, what }: { error: ApiError; what: string }) {
  const kind =
    error.status === 0 ? 'backend' :
    error.status === 502 ? 'gateway' :
    error.status === 503 ? 'store' :
    error.status === 504 ? 'timeout' :
    error.status === 404 ? 'missing' : 'refused'

  const said: Record<string, string> = {
    backend: 'the ui backend did not answer. is it running?',
    gateway: 'the gateway is down. start it with `make query`.',
    store: 'the store is unavailable. the gateway is up, redis is not.',
    timeout: 'the gateway took too long and the request was given up on.',
    missing: `${what} was never seen in any processed record.`,
    refused: error.message,
  }

  return (
    <div className={`failure ${kind}`}>
      <strong>{kind === 'missing' ? 'not found' : kind === 'refused' ? 'refused' : `${kind} error`}</strong>
      <span>{said[kind]}</span>
      <code>{error.status ? `http ${error.status}` : 'no answer'}</code>
    </div>
  )
}

export function SearchIcon() {
  return (
    <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" className="icon">
      <circle cx="7" cy="7" r="4.6" fill="none" stroke="currentColor" strokeWidth="1.6" />
      <path d="M10.6 10.6 14 14" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
    </svg>
  )
}

export function Loading({ what }: { what: string }) {
  return <div className="loading">reading {what}…</div>
}

export function Stale({ at }: { at: number }) {
  return <span className="stale">as of {ago(at)}</span>
}

export function Picker<T extends string>({
  options,
  chosen,
  onPick,
}: {
  options: readonly T[]
  chosen: T
  onPick: (option: T) => void
}) {
  return (
    <div className="picker">
      {options.map((option) => (
        <button key={option} className={option === chosen ? 'on' : ''} onClick={() => onPick(option)}>
          {option}
        </button>
      ))}
    </div>
  )
}

export function Pager({
  offset,
  limit,
  count,
  onMove,
}: {
  offset: number
  limit: number
  count: number
  onMove: (offset: number) => void
}) {
  const last = Math.max(0, Math.floor((count - 1) / limit) * limit)
  return (
    <div className="pager">
      <button disabled={offset <= 0} onClick={() => onMove(Math.max(0, offset - limit))}>previous</button>
      <span>
        {count ? offset + 1 : 0}–{Math.min(offset + limit, count)} of {count.toLocaleString()}
      </span>
      <button disabled={offset >= last} onClick={() => onMove(Math.min(last, offset + limit))}>next</button>
    </div>
  )
}
