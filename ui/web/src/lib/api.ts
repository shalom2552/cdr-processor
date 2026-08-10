export class ApiError extends Error {
  constructor(readonly status: number, readonly body: any) {
    super(body?.error ?? `http ${status}`)
  }
}

export type Health = {
  status: string
  store: 'up' | 'down'
  keys: number
  'max-hops': number
  'max-visited': number
}

export type Totals = Record<string, number>

export type Board = 'voice' | 'sms' | 'data' | 'fail' | 'op-voice' | 'op-sms'

export type Top = {
  board: string
  count: number
  offset: number
  limit: number
  entries: { id: string; score: number }[]
}

export type Subscriber = {
  msisdn: string
  'voice-out': number
  'voice-in': number
  'data-out': number
  'data-in': number
  'sms-out': number
  'sms-in': number
  'no-answer': number
  busy: number
  failed: number
}

export type Operator = {
  mccmnc: string
  'voice-out': number
  'voice-in': number
  'sms-out': number
  'sms-in': number
}

export type Peer = { msisdn: string; duration: number; calls: number; sms: number }

export type Peers = {
  msisdn: string
  count: number
  offset: number
  limit: number
  sort: string
  peers: Peer[]
}

export type Link = {
  'first-party': string
  'second-party': string
  duration: number
  calls: number
  sms: number
}

export type Hop = { from: string; to: string; duration: number; calls: number; sms: number }

export type PathResult = { path: string[]; hops?: Hop[] }

export type Series = {
  metric: string
  window: string
  seconds: number
  from: number
  to: number
  points: [number, number | null][]
}

export type ConfigKey = { key: string; value: string; help: string }

export type ConfigSection = {
  name: string
  help: string
  keys: ConfigKey[]
  active: boolean
  reason: string
}

export type ConfigDoc = { path: string; sections: ConfigSection[]; selected: string[] }

export type System = {
  gateway: { url: string; status: number; health: any }
  store: { host: string; port: number }
  sampler: {
    running: boolean
    interval: number
    started: number
    polls: number
    failures: number
    last_sample: number
    last_error: string
    db: { path: string; rows: number; oldest: number | null; newest: number | null; bytes: number }
  }
  routes: Record<string, { status: number; at: number; error: string }>
  config: string
  retention_days: number
}

async function get<T>(url: string, signal?: AbortSignal): Promise<T> {
  let response: Response
  try {
    response = await fetch(url, { signal })
  } catch (failure) {
    if ((failure as Error).name === 'AbortError') throw failure
    throw new ApiError(0, { error: 'the ui backend did not answer' })
  }

  const body = await response.json().catch(() => ({ error: 'no json in the answer' }))
  if (!response.ok) throw new ApiError(response.status, body)
  return body as T
}

const query = (params: Record<string, string | number>) =>
  '?' + new URLSearchParams(Object.entries(params).map(([k, v]) => [k, String(v)]))

/* Health is read by the status pill and by whatever screen is open, on the same clock.
   One answer serves both while it is fresh, so the gateway sees one call, not two. */
let cached: { at: number; answer: Promise<Health> } | null = null
const HEALTH_TTL = 2000

function health(signal?: AbortSignal): Promise<Health> {
  const now = Date.now()
  if (cached && now - cached.at < HEALTH_TTL) return cached.answer

  const answer = get<Health>('/api/health', signal)
  cached = { at: now, answer }
  answer.catch(() => { cached = null })
  return answer
}

export const api = {
  health,
  totals: (signal?: AbortSignal) => get<Totals>('/api/totals', signal),
  top: (board: string, limit: number, offset: number, signal?: AbortSignal) =>
    get<Top>(`/api/top/${board}` + query({ limit, offset }), signal),
  subscriber: (msisdn: string, signal?: AbortSignal) =>
    get<Subscriber>(`/api/subscriber/${msisdn}`, signal),
  operator: (mccmnc: string, signal?: AbortSignal) =>
    get<Operator>(`/api/operator/${mccmnc}`, signal),
  peers: (msisdn: string, sort: string, limit: number, offset: number, signal?: AbortSignal) =>
    get<Peers>(`/api/peers/${msisdn}` + query({ sort, limit, offset }), signal),
  link: (first: string, second: string, signal?: AbortSignal) =>
    get<Link>(`/api/link/${first}/${second}`, signal),
  path: (first: string, second: string, signal?: AbortSignal) =>
    get<PathResult>(`/api/path/${first}/${second}`, signal),
  series: (metric: string, window: string, signal?: AbortSignal) =>
    get<Series>('/api/series' + query({ metric, window }), signal),
  config: (signal?: AbortSignal) => get<ConfigDoc>('/api/config', signal),
  system: (signal?: AbortSignal) => get<System>('/api/system', signal),
}
