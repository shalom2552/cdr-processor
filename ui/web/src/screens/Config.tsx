import { api } from '../lib/api'
import { useLoad } from '../lib/hooks'
import { Failure, Loading, Panel } from '../components/parts'

export default function ConfigScreen() {
  const { data, error, loading } = useLoad((signal) => api.config(signal), [])

  if (error) return <Failure error={error} what="config.toml" />
  if (loading && !data) return <Loading what="config.toml" />
  if (!data) return null

  return (
    <div className="screen">
      <p className="banner">
        this is the file on disk, <code>{data.path}</code>. config is read once at startup and never
        reloaded, so a process started before the last edit is running something else. read-only here.
      </p>

      {data.sections.map((section) => (
        <Panel
          key={section.name}
          title={section.name}
          right={!section.active && <span className="inactive">inactive · {section.reason}</span>}
        >
          <div className={section.active ? '' : 'dimmed'}>
            {section.help && <p className="quiet">{section.help}</p>}
            <table className="grid">
              <tbody>
                {section.keys.map((key) => (
                  <tr key={key.key}>
                    <th>{key.key}</th>
                    <td><code>{key.value}</code></td>
                    <td className="quiet">{key.help}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Panel>
      ))}
    </div>
  )
}
