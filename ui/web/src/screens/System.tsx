import { api } from '../lib/api'
import { ago, bytes, clock, count } from '../lib/format'
import { useLoad } from '../lib/hooks'
import { useSettings } from '../lib/settings'
import { Failure, Loading, Panel, Tile } from '../components/parts'

export default function SystemScreen() {
  const settings = useSettings()
  const { data, error, loading } = useLoad((signal) => api.system(signal), [], settings.refresh)

  if (error) return <Failure error={error} what="the system view" />
  if (loading && !data) return <Loading what="the system view" />
  if (!data) return null

  const health = data.gateway.status === 200 ? data.gateway.health : null
  const sampler = data.sampler

  return (
    <div className="screen">
      <Panel title="gateway">
        <div className="tiles">
          <Tile label="answered" value={data.gateway.status ? `http ${data.gateway.status}` : 'no answer'} />
          <Tile label="store" value={health?.store ?? 'unknown'} />
          <Tile label="keys" value={health ? count(health.keys) : '—'} />
          <Tile label="max hops" value={health ? String(health['max-hops']) : '—'} />
        </div>
        <table className="grid">
          <tbody>
            <tr><th>reached at</th><td><code>{data.gateway.url}</code></td></tr>
            <tr><th>store address</th><td><code>{data.store.host}:{data.store.port}</code></td></tr>
            <tr><th>config file</th><td><code>{data.config}</code></td></tr>
            <tr><th>max visited</th><td>{health ? count(health['max-visited']) : '—'}</td></tr>
          </tbody>
        </table>
        {!health && <p className="quiet">{JSON.stringify(data.gateway.health)}</p>}
      </Panel>

      <Panel title="sampler">
        <div className="tiles">
          <Tile label="state" value={sampler.running ? 'running' : 'stopped'} />
          <Tile label="interval" value={`${sampler.interval}s`} />
          <Tile label="last sample" value={ago(sampler.last_sample)} note={clock(sampler.last_sample)} />
          <Tile label="polls" value={count(sampler.polls)} note={`${count(sampler.failures)} failed`} />
          <Tile label="rows stored" value={count(sampler.db.rows)} note={`kept ${data.retention_days} days`} />
          <Tile label="oldest row" value={sampler.db.oldest ? clock(sampler.db.oldest) : '—'} />
          <Tile label="database" value={bytes(sampler.db.bytes)} />
          <Tile label="last error" value={sampler.last_error ? 'yes' : 'none'} note={sampler.last_error} />
        </div>
      </Panel>

      <Panel title="the gateway routes the ui depends on">
        <table className="grid">
          <thead><tr><th>route</th><th>last status</th><th>when</th><th>error</th></tr></thead>
          <tbody>
            {Object.entries(data.routes).map(([route, seen]) => (
              <tr key={route}>
                <td><code>{route}</code></td>
                <td className={seen.status === 200 ? 'good' : 'bad'}>{seen.status || 'no answer'}</td>
                <td>{ago(seen.at)}</td>
                <td className="quiet">{seen.error}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </Panel>
    </div>
  )
}
