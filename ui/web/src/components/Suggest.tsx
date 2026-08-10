import { api } from '../lib/api'
import { useLoad } from '../lib/hooks'

/* Candidates to look up, off a board. Nothing else can offer any: listing the store would
   walk the keyspace, and a board is the one ranked set the processor keeps. */
export function Suggest({
  board = 'voice',
  what = 'nothing in mind?',
  onPick,
}: {
  board?: string
  what?: string
  onPick: (id: string) => void
}) {
  const { data } = useLoad((signal) => api.top(board, 5, 0, signal), [board])
  if (!data || data.entries.length === 0) return null

  return (
    <div className="suggest">
      <span className="suggest-label">{what} the heaviest on {board}:</span>
      <div className="suggest-chips">
        {data.entries.map((entry) => (
          <button key={entry.id} className="chip" onClick={() => onPick(entry.id)}>{entry.id}</button>
        ))}
      </div>
      <a className="suggest-more" href={`#/rankings/${board}`}>more</a>
    </div>
  )
}
