import { api } from './lib/api'
import { ago } from './lib/format'
import { forget, routeOf, useHistory } from './lib/history'
import { useLoad, useNow, useRoute } from './lib/hooks'
import { useSettings } from './lib/settings'
import Dashboard from './screens/Dashboard'
import SubscriberScreen, { LinkScreen } from './screens/Subscriber'
import Rankings from './screens/Rankings'
import GraphScreen from './screens/Graph'
import PathScreen from './screens/Path'
import OperatorScreen from './screens/Operator'
import ConfigScreen from './screens/Config'
import SystemScreen from './screens/System'
import SettingsScreen from './screens/Settings'

const SCREENS: [string, string][] = [
  ['', 'Dashboard'],
  ['subscriber', 'Subscriber'],
  ['rankings', 'Rankings'],
  ['graph', 'Graph'],
  ['path', 'Path'],
  ['operator', 'Operator'],
  ['config', 'Config'],
  ['system', 'System'],
  ['settings', 'Settings'],
]

function StatusPill() {
  const settings = useSettings()
  const now = useNow()
  const { data, error, at } = useLoad((signal) => api.health(signal), [], settings.refresh)

  const state = error ? (error.status === 0 ? 'backend down' : 'gateway down')
    : !data ? 'checking' : data.store === 'up' ? 'store up' : 'store down'
  const tone = error ? 'bad' : !data ? '' : data.store === 'up' ? 'good' : 'warn'
  const stale = at > 0 && now - at > settings.refresh * 2

  return (
    <div className={`pill ${tone} ${stale ? 'stale' : ''}`}>
      <span className="dot" />
      <span>{state}</span>
      {data && <span className="pill-keys">{data.keys.toLocaleString()} keys</span>}
      {at > 0 && <span className="pill-age">{ago(at)}</span>}
    </div>
  )
}

function Rail({ active }: { active: string }) {
  const history = useHistory()

  return (
    <nav className="rail">
      <div className="brand">CDR-Insight</div>
      <ul>
        {SCREENS.map(([route, name]) => (
          <li key={route}>
            <a className={route === active ? 'on' : ''} href={`#/${route}`}>{name}</a>
          </li>
        ))}
      </ul>

      <div className="history">
        <h3>
          recent
          {history.length > 0 && <button className="clear" onClick={forget}>clear</button>}
        </h3>
        <ul>
          {history.slice(0, 12).map((lookup) => (
            <li key={`${lookup.kind}:${lookup.id}`}>
              <a href={routeOf(lookup)}>
                <span className="kind">{lookup.kind}</span>
                {lookup.id}
              </a>
            </li>
          ))}
          {history.length === 0 && <li className="quiet">nothing looked up yet</li>}
        </ul>
      </div>
    </nav>
  )
}

export default function App() {
  const route = useRoute()
  const screen = route[0] ?? ''
  const rest = route.slice(1)

  const body = () => {
    switch (screen) {
      case 'subscriber': return <SubscriberScreen msisdn={rest[0]} />
      case 'link': return <LinkScreen first={rest[0]} second={rest[1]} />
      case 'rankings': return <Rankings board={rest[0]} />
      case 'graph': return <GraphScreen msisdn={rest[0]} />
      case 'path': return <PathScreen first={rest[0]} second={rest[1]} />
      case 'operator': return <OperatorScreen mccmnc={rest[0]} />
      case 'config': return <ConfigScreen />
      case 'system': return <SystemScreen />
      case 'settings': return <SettingsScreen />
      default: return <Dashboard />
    }
  }

  const title = SCREENS.find(([route]) => route === screen)?.[1] ?? 'Dashboard'

  return (
    <div className="app">
      <Rail active={SCREENS.some(([route]) => route === screen) ? screen : ''} />
      <div className="main">
        <header className="top">
          <div className="top-inner">
            <h1>{title}</h1>
            <StatusPill />
          </div>
        </header>
        <main>{body()}</main>
      </div>
    </div>
  )
}
