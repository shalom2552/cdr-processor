import { DEFAULTS, resetSettings, setSettings, useSettings, type Settings } from '../lib/settings'
import { Panel, Picker } from '../components/parts'

const NUMBERS: [keyof Settings, string, string][] = [
  ['refresh', 'refresh seconds', 'how often a screen re-reads the backend'],
  ['peerPage', 'peers per page', 'the peer list on the subscriber screen'],
  ['topPage', 'entries per page', 'a ranking board'],
  ['expandLimit', 'expand limit', 'peers pulled per graph expansion, heaviest first'],
  ['maxNodes', 'max nodes', 'nodes allowed on the graph canvas'],
]

export default function SettingsScreen() {
  const settings = useSettings()

  return (
    <div className="screen">
      <p className="banner info">
        these are this browser's settings, kept in local storage. nothing here is sent to the
        backend and nothing here changes <code>config.toml</code>.
      </p>

      <Panel title="appearance">
        <label className="setting">
          <span>theme</span>
          <Picker options={['system', 'dark', 'light'] as const} chosen={settings.theme}
                  onPick={(theme) => setSettings({ theme })} />
        </label>
        <label className="setting">
          <span>default window</span>
          <Picker options={['15m', '1h', '6h', '24h', 'all'] as const} chosen={settings.window as any}
                  onPick={(window) => setSettings({ window })} />
        </label>
        <label className="setting">
          <span>graph edge metric</span>
          <Picker options={['duration', 'sms'] as const} chosen={settings.edgeMetric}
                  onPick={(edgeMetric) => setSettings({ edgeMetric })} />
        </label>
      </Panel>

      <Panel title="limits" right={<button className="ghost" onClick={resetSettings}>reset to defaults</button>}>
        {NUMBERS.map(([key, label, help]) => (
          <label className="setting" key={key}>
            <span>{label}</span>
            <input
              type="number"
              min={1}
              value={settings[key] as number}
              onChange={(event) => setSettings({ [key]: Math.max(1, Number(event.target.value)) } as Partial<Settings>)}
            />
            <em className="setting-help">{help}, default {String(DEFAULTS[key])}</em>
          </label>
        ))}
      </Panel>
    </div>
  )
}
