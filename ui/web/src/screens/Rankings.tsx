import { useState } from 'react'
import { api } from '../lib/api'
import { toCsv } from '../lib/exports'
import { count, share, value as formatted, type Unit } from '../lib/format'
import { useLoad } from '../lib/hooks'
import { useSettings } from '../lib/settings'
import { Failure, Loading, Pager, Panel, Picker } from '../components/parts'

const BOARDS = ['voice', 'sms', 'data', 'fail', 'op-voice', 'op-sms'] as const
type BoardName = (typeof BOARDS)[number]

const UNITS: Record<BoardName, Unit> = {
  voice: 'seconds',
  sms: 'count',
  data: 'bytes',
  fail: 'count',
  'op-voice': 'seconds',
  'op-sms': 'count',
}

const SAID: Record<BoardName, string> = {
  voice: 'call seconds, out and in together',
  sms: 'messages, out and in together',
  data: 'bytes, rx and tx together',
  fail: 'no-answer, busy and failed together. this is the largest absolute count, not the worst rate: a heavy user out-ranks a suspicious one.',
  'op-voice': 'operator call seconds',
  'op-sms': 'operator messages',
}

function GraphIcon() {
  return (
    <svg className="icon" viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
      <path d="M8 4.5 3.4 11.2M8 4.5 12.6 11.2M3.4 11.2h9.2" fill="none" stroke="currentColor" strokeWidth="1.2" />
      <circle cx="8" cy="3.8" r="2" fill="currentColor" />
      <circle cx="3.2" cy="11.8" r="2" fill="currentColor" />
      <circle cx="12.8" cy="11.8" r="2" fill="currentColor" />
    </svg>
  )
}

export default function Rankings({ board }: { board?: string }) {
  const settings = useSettings()
  const chosen = (BOARDS.includes(board as BoardName) ? board : 'voice') as BoardName
  const [offset, setOffset] = useState(0)

  const { data, error, loading } = useLoad(
    (signal) => api.top(chosen, settings.topPage, offset, signal),
    [chosen, offset, settings.topPage],
  )

  const leader = data?.entries[0]?.score ?? 0
  const operators = chosen.startsWith('op-')

  return (
    <div className="screen">
      <Panel
        title={data ? `${chosen}, ${count(data.count)} ranked` : chosen}
        right={
          data && data.entries.length > 0 ? (
            <button
              className="ghost"
              onClick={() =>
                toCsv(`${chosen}-board.csv`, [
                  ['position', 'id', 'score'],
                  ...data.entries.map((entry, index) => [offset + index + 1, entry.id, entry.score]),
                ])
              }
            >
              csv
            </button>
          ) : undefined
        }
      >
        <div className="board-picker">
          <Picker
            options={BOARDS}
            chosen={chosen}
            onPick={(pick) => {
              setOffset(0)
              window.location.hash = `#/rankings/${pick}`
            }}
          />
        </div>

        <p className="board-note">{SAID[chosen]}</p>

        {error && <Failure error={error} what="the board" />}
        {loading && !data && <Loading what={`the ${chosen} board`} />}
        {data && data.entries.length === 0 && (
          <p className="quiet">the board is empty. nothing has been processed into it yet.</p>
        )}

        {data && data.entries.length > 0 && (
          <>
            <table className="grid ranked">
              <thead>
                <tr>
                  <th>#</th>
                  <th>{operators ? 'operator' : 'subscriber'}</th>
                  <th className="num">score</th>
                  <th className="num">of the leader</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {data.entries.map((entry, index) => (
                  <tr key={entry.id}>
                    <td className="rank">{offset + index + 1}</td>
                    <td>
                      <a href={operators ? `#/operator/${entry.id}` : `#/subscriber/${entry.id}`}>{entry.id}</a>
                    </td>
                    <td className="num">{formatted(entry.score, UNITS[chosen])}</td>
                    <td className="num">{share(leader ? entry.score / leader : null)}</td>
                    <td className="actions">
                      {!operators && (
                        <a className="icon-link" href={`#/graph/${entry.id}`} title="on the graph">
                          <GraphIcon />
                        </a>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>

            <Pager offset={offset} limit={settings.topPage} count={data.count} onMove={setOffset} />
          </>
        )}
      </Panel>
    </div>
  )
}
