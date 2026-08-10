import { api, type Series, type Totals } from '../lib/api'
import { bytes, count, duration, share, value as formatted } from '../lib/format'
import { useLoad } from '../lib/hooks'
import { setSettings, useSettings } from '../lib/settings'
import { Donut, LineChart, StackedArea, type Point } from '../components/charts'
import { Failure, Loading, Panel, Picker, Stale, Tile } from '../components/parts'

const WINDOWS = ['15m', '1h', '6h', '24h', 'all'] as const

const MIX: [string, string][] = [
  ['moc-cnt', 'voice out'],
  ['mtc-cnt', 'voice in'],
  ['sms-mo-cnt', 'sms out'],
  ['sms-mt-cnt', 'sms in'],
  ['data-cnt', 'data'],
  ['noans-cnt', 'no answer'],
  ['busy-cnt', 'busy'],
  ['failed-cnt', 'failed'],
]

const BOARDS: [string, string, 'count' | 'seconds' | 'bytes'][] = [
  ['voice', 'top voice', 'seconds'],
  ['sms', 'top sms', 'count'],
  ['data', 'top data', 'bytes'],
  ['fail', 'top failures', 'count'],
  ['op-voice', 'top operators, voice', 'seconds'],
]

const SPARKS = ['records:rate', 'calls:rate', 'messages:rate', 'data-cnt:rate', 'data-vol:rate', 'fail-share', 'keys']

function last(series: Series | undefined): number | null {
  const points = series?.points ?? []
  for (let index = points.length - 1; index >= 0; index -= 1) {
    if (points[index][1] !== null) return points[index][1]
  }
  return null
}

function spark(series: Series | undefined): Point[] {
  return (series?.points ?? []) as Point[]
}

function Leaders({ board, title, unit }: { board: string; title: string; unit: 'count' | 'seconds' | 'bytes' }) {
  const { data, error } = useLoad((signal) => api.top(board, 5, 0, signal), [board])

  if (error) return <div className="leader"><h3>{title}</h3><Failure error={error} what="the board" /></div>

  return (
    <div className="leader">
      <h3>{title}</h3>
      {data && data.entries.length === 0 && <p className="quiet">empty board, nothing processed yet</p>}
      <ol>
        {data?.entries.map((entry) => (
          <li key={entry.id}>
            <a href={board.startsWith('op-') ? `#/operator/${entry.id}` : `#/subscriber/${entry.id}`}>{entry.id}</a>
            <span>{formatted(entry.score, unit)}</span>
          </li>
        ))}
      </ol>
      <a className="more" href={`#/rankings/${board}`}>more</a>
    </div>
  )
}

export default function Dashboard() {
  const settings = useSettings()
  const range = WINDOWS.includes(settings.window as (typeof WINDOWS)[number]) ? settings.window : '1h'

  const store = useLoad(
    async (signal) => ({ totals: await api.totals(signal), health: await api.health(signal) }),
    [],
    settings.refresh,
  )

  const curves = useLoad(
    async (signal) => {
      const wanted = [...SPARKS, ...MIX.map(([metric]) => `${metric}:rate`)]
      const answered = await Promise.all(wanted.map((metric) => api.series(metric, range, signal)))
      return Object.fromEntries(answered.map((series) => [series.metric, series])) as Record<string, Series>
    },
    [range],
    settings.refresh,
  )

  if (store.error) return <Failure error={store.error} what="the store" />
  if (!store.data) return <Loading what="the totals" />

  const totals: Totals = store.data.totals
  const at = (name: string) => totals[name] ?? 0
  const records = at('records')
  const calls = at('moc-cnt') + at('mtc-cnt')
  const messages = at('sms-mo-cnt') + at('sms-mt-cnt')
  const volume = at('data-rx') + at('data-tx')
  const failures = at('noans-cnt') + at('busy-cnt') + at('failed-cnt')

  const series = curves.data ?? {}
  const points = (metric: string) => spark(series[metric])
  const samples = points('records:rate').length
  const rateNote = samples < 1 ? `no rate yet, the first curve needs ${settings.refresh * 2}s of samples` : undefined

  const mix = MIX.map(([metric, label]) => ({ label, value: at(metric) }))
  const bands = MIX.map(([metric, label]) => ({ label, points: points(`${metric}:rate`) }))
  const outcomes = [
    { label: 'answered', points: points('calls:rate') },
    { label: 'no answer', points: points('noans-cnt:rate') },
    { label: 'busy', points: points('busy-cnt:rate') },
    { label: 'failed', points: points('failed-cnt:rate') },
  ]

  return (
    <div className="screen">
      <div className="strip">
        <span className={store.data.health.store === 'up' ? 'good' : 'bad'}>
          store {store.data.health.store}
        </span>
        <span>{count(store.data.health.keys)} keys, every kind together</span>
        <span>{samples ? `${samples} samples in ${range}` : 'no samples yet'}</span>
        <Stale at={store.at} />
      </div>

      {records === 0 && (
        <p className="first-run">
          the store is empty. nothing has been processed into it yet — run the processor and the
          tiles below fill in.
        </p>
      )}

      <div className="tiles kpi">
        <Tile label="records" value={count(records)} note="since store creation"
              spark={points('records:rate')} delta={rateNote ?? `${formatted(last(series['records:rate']), 'rate')} now`} />
        <Tile label="calls" value={count(calls)} note="moc + mtc"
              spark={points('calls:rate')} delta={`${formatted(last(series['calls:rate']), 'rate')} now`} />
        <Tile label="messages" value={count(messages)} note="sms-mo + sms-mt"
              spark={points('messages:rate')} delta={`${formatted(last(series['messages:rate']), 'rate')} now`} />
        <Tile label="data sessions" value={count(at('data-cnt'))} note="since store creation"
              spark={points('data-cnt:rate')} delta={`${formatted(last(series['data-cnt:rate']), 'rate')} now`} />
        <Tile label="data volume" value={bytes(volume)} note="rx + tx" spark={points('data-vol:rate')} />
        <Tile label="failed attempts" value={count(failures)} note="no-answer, busy and failed" />
        <Tile label="failure share" value={share(records ? failures / records : null)}
              note="of every record processed" spark={points('fail-share')} />
        <Tile label="keys in store" value={count(store.data.health.keys)}
              note="subscribers, operators, links, boards and totals" />
      </div>

      <Panel
        title="throughput"
        right={
          <div className="window-row">
            <Picker options={WINDOWS} chosen={range as (typeof WINDOWS)[number]}
                    onPick={(pick) => setSettings({ window: pick })} />
          </div>
        }
      >
        <LineChart points={points('records:rate')} unit="rate" label="records per second"
                   empty={rateNote ?? 'no samples in this window yet'} />
      </Panel>

      <div className="two">
        <Panel title="usage mix">
          <Donut slices={mix} unit="count" />
        </Panel>
        <Panel title="usage mix over time">
          <StackedArea bands={bands} />
        </Panel>
      </div>

      <Panel title="outcomes" right={<span className="quiet">share of what was attempted</span>}>
        <StackedArea bands={outcomes} />
      </Panel>

      <Panel title="durations" right={<span className="quiet">lifetime averages</span>}>
        <div className="tiles">
          <Tile label="average call out" value={duration(at('moc-cnt') ? at('moc-dur') / at('moc-cnt') : null)}
                note="moc-dur over moc-cnt" />
          <Tile label="average call in" value={duration(at('mtc-cnt') ? at('mtc-dur') / at('mtc-cnt') : null)}
                note="mtc-dur over mtc-cnt" />
          <Tile label="average data session" value={duration(at('data-cnt') ? at('data-dur') / at('data-cnt') : null)}
                note="data-dur over data-cnt" />
        </div>
      </Panel>

      <Panel title="leaders">
        <div className="leaders">
          {BOARDS.map(([board, title, unit]) => (
            <Leaders key={board} board={board} title={title} unit={unit} />
          ))}
        </div>
      </Panel>
    </div>
  )
}
