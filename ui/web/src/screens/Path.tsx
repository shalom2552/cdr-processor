import { useEffect, useRef, useState } from 'react'
import { ApiError, api, type Link, type PathResult } from '../lib/api'
import { toJson } from '../lib/exports'
import { count, duration } from '../lib/format'
import { go, useLoad } from '../lib/hooks'
import { remember } from '../lib/history'
import { Failure, Loading, Panel, SearchIcon } from '../components/parts'
import { Suggest } from '../components/Suggest'

export default function PathScreen({ first, second }: { first?: string; second?: string }) {
  const [a, setA] = useState(first ?? '')
  const [b, setB] = useState(second ?? '')
  const [result, setResult] = useState<PathResult | null>(null)
  const [error, setError] = useState<ApiError | null>(null)
  const [searching, setSearching] = useState(false)
  const abort = useRef<AbortController | null>(null)

  const health = useLoad((signal) => api.health(signal), [])
  const direct = useLoad<Link | null>(
    (signal) => (first && second ? api.link(first, second, signal).catch(() => null) : Promise.resolve(null)),
    [first, second],
  )

  useEffect(() => {
    setA(first ?? '')
    setB(second ?? '')
    if (!first || !second) {
      setResult(null)
      setError(null)
      return
    }

    const controller = new AbortController()
    abort.current = controller
    setSearching(true)
    setResult(null)
    setError(null)

    api.path(first, second, controller.signal)
      .then((answer) => setResult(answer))
      .catch((failure) => {
        if (failure.name === 'AbortError') return
        setError(failure instanceof ApiError ? failure : new ApiError(0, { error: String(failure) }))
      })
      .finally(() => setSearching(false))

    return () => controller.abort()
  }, [first, second])

  const submit = (event: React.FormEvent) => {
    event.preventDefault()
    const from = a.match(/\d+/g)?.[0]
    const to = b.match(/\d+/g)?.[0]
    if (!from || !to) return
    remember('path', `${from}/${to}`)
    go(`path/${from}/${to}`)
  }

  const bounds = health.data
    ? `at most ${health.data['max-hops']} hops and ${count(health.data['max-visited'])} subscribers read`
    : 'the search is bounded by max-hops and max-visited'

  return (
    <div className="screen">
      <Panel title="look up">
        <form className="entry" onSubmit={submit}>
          <div className="entry-row">
            <input inputMode="numeric" placeholder="from msisdn" value={a} onChange={(event) => setA(event.target.value)} />
            <input inputMode="numeric" placeholder="to msisdn" value={b} onChange={(event) => setB(event.target.value)} />
            <button type="submit" aria-label="search" title="search"><SearchIcon /></button>
            {searching && (
              <button type="button" className="ghost" onClick={() => { abort.current?.abort(); setSearching(false) }}>
                cancel
              </button>
            )}
          </div>
        </form>

        {!first && !second && (
          <Suggest
          what="no pair in mind? a subscriber and its heaviest peer, from"
          onPick={async (id) => {
            const peers = await api.peers(id, 'dur', 1, 0).catch(() => null)
            const peer = peers?.peers[0]?.msisdn
            if (!peer) return
            remember('path', `${id}/${peer}`)
            go(`path/${id}/${peer}`)
            }}
          />
        )}
      </Panel>

      {searching && <Loading what="a path, which can take seconds" />}

      {error && error.status === 504 && (
        <div className="failure timeout">
          <strong>timed out</strong>
          <span>the search was given up on before it answered. that is not the same as no path.</span>
          <code>http 504</code>
        </div>
      )}
      {error && error.status === 404 && (
        <div className="failure missing">
          <strong>no path</strong>
          <span>none found within the bounds: {bounds}.</span>
          <code>http 404</code>
        </div>
      )}
      {error && error.status !== 404 && error.status !== 504 && <Failure error={error} what="the path" />}

      {result && (
        <Panel
          title={`${result.path.length - 1} hops`}
          right={<button className="ghost" onClick={() => toJson(`path-${first}-${second}.json`, result)}>json</button>}
        >
          <ol className="chain">
            {result.path.map((node, index) => (
              <li key={node}>
                <a href={`#/subscriber/${node}`}>{node}</a>
                {result.hops?.[index] && (
                  <span className="hop">
                    {duration(result.hops[index].duration)} · {count(result.hops[index].sms)} sms
                  </span>
                )}
              </li>
            ))}
          </ol>
        </Panel>
      )}

      {first && second && (
        <Panel title="the direct link">
          {direct.data ? (
            <p>
              {direct.data['first-party']} ↔ {direct.data['second-party']}: {duration(direct.data.duration)},{' '}
              {count(direct.data.sms)} messages. a path of length one and a link are the same fact.
            </p>
          ) : (
            <p className="quiet">these two are in no link of their own.</p>
          )}
        </Panel>
      )}
    </div>
  )
}
