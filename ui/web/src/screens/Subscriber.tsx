import { useState } from 'react'
import { api, type Link, type Subscriber } from '../lib/api'
import { toCsv } from '../lib/exports'
import { bytes, count, duration, share } from '../lib/format'
import { go, useLoad } from '../lib/hooks'
import { remember } from '../lib/history'
import { useSettings } from '../lib/settings'
import { PairedBars } from '../components/charts'
import { Suggest } from '../components/Suggest'
import { Failure, Loading, Pager, Panel, Picker, SearchIcon, Tile } from '../components/parts'

const TABS = ['subscriber', 'operator', 'link', 'path'] as const
type Tab = (typeof TABS)[number]

/* The search box takes digits: one number is a subscriber, two are a link and a path.
   The tabs below it are the same lookups with no guessing. */
export function Entry({ tab = 'subscriber' }: { tab?: Tab }) {
  const [chosen, setChosen] = useState<Tab>(tab)
  const [first, setFirst] = useState('')
  const [second, setSecond] = useState('')

  const submit = (event: React.FormEvent) => {
    event.preventDefault()
    const digits = first.match(/\d+/g) ?? []
    const a = digits[0]
    const b = second.match(/\d+/g)?.[0] ?? digits[1]
    if (!a) return

    if (chosen === 'operator') return open('operator', a)
    if (chosen === 'path' && b) return open('path', `${a}/${b}`)
    if (chosen === 'link' && b) return open('link', `${a}/${b}`)
    if (b) return open('link', `${a}/${b}`)
    open('subscriber', a)
  }

  const open = (kind: 'subscriber' | 'operator' | 'link' | 'path', id: string) => {
    remember(kind, id)
    go(`${kind}/${id}`)
  }

  return (
      <form className="entry" onSubmit={submit}>
        <Picker options={TABS} chosen={chosen} onPick={setChosen} />
        <div className="entry-row">
          <input
            inputMode="numeric"
            placeholder={chosen === 'operator' ? 'mccmnc' : 'msisdn, or two for a link or a path'}
            value={first}
            onChange={(event) => setFirst(event.target.value)}
          />
          {(chosen === 'link' || chosen === 'path') && (
            <input
              inputMode="numeric"
              placeholder="second msisdn"
              value={second}
              onChange={(event) => setSecond(event.target.value)}
            />
          )}
          <button type="submit" aria-label="search" title="search"><SearchIcon /></button>
        </div>
      </form>
  )
}

function Peers({ msisdn, voice }: { msisdn: string; voice: number }) {
  const settings = useSettings()
  const [sort, setSort] = useState<'dur' | 'sms'>('dur')
  const [offset, setOffset] = useState(0)

  const { data, error, loading } = useLoad(
    (signal) => api.peers(msisdn, sort, settings.peerPage, offset, signal),
    [msisdn, sort, offset, settings.peerPage],
  )

  if (error && error.status === 404) return <p className="quiet">no peers: this subscriber is in no link.</p>
  if (error) return <Failure error={error} what="the peer list" />
  if (loading && !data) return <Loading what="the peers" />
  if (!data) return null

  return (
    <>
      <div className="row-between">
        <Picker options={['dur', 'sms'] as const} chosen={sort} onPick={setSort} />
        <span className="quiet">
          {count(data.count)} peers in all{voice ? `, ${duration(voice / data.count)} of voice each` : ''}
        </span>
        <button
          className="ghost"
          onClick={() =>
            toCsv(`peers-${msisdn}.csv`, [
              ['msisdn', 'calls', 'duration', 'sms'],
              ...data.peers.map((peer) => [peer.msisdn, peer.calls, peer.duration, peer.sms]),
            ])
          }
        >
          csv
        </button>
      </div>

      <table className="grid">
        <thead>
          <tr><th>peer</th><th className="num">calls</th><th className="num">duration</th><th className="num">messages</th><th /></tr>
        </thead>
        <tbody>
          {data.peers.map((peer) => (
            <tr key={peer.msisdn}>
              <td><a href={`#/subscriber/${peer.msisdn}`}>{peer.msisdn}</a></td>
              <td className="num">{count(peer.calls)}</td>
              <td className="num">{duration(peer.duration)}</td>
              <td className="num">{count(peer.sms)}</td>
              <td className="actions">
                <a href={`#/link/${msisdn}/${peer.msisdn}`}>link</a>
                <a href={`#/graph/${peer.msisdn}`}>graph</a>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <Pager offset={offset} limit={settings.peerPage} count={data.count} onMove={setOffset} />
    </>
  )
}

export function LinkScreen({ first, second }: { first?: string; second?: string }) {
  const { data, error, loading } = useLoad<Link | null>(
    (signal) => (first && second ? api.link(first, second, signal) : Promise.resolve(null)),
    [first, second],
  )

  if (!first || !second) return <div className="screen"><Panel title="look up"><Entry tab="link" /></Panel></div>

  return (
    <div className="screen">
      <Panel title="look up"><Entry tab="link" /></Panel>
      {loading && !data && <Loading what="the link" />}
      {error && <Failure error={error} what="this pair" />}
      {data && (
        <Panel title={`${data['first-party']} ↔ ${data['second-party']}`}>
          <div className="tiles">
            <Tile label="calls" value={count(data.calls)} />
            <Tile label="duration" value={duration(data.duration)} />
            <Tile label="average call" value={duration(data.calls ? data.duration / data.calls : null)}
                  note={data.calls ? undefined : 'no calls counted for this pair'} />
            <Tile label="messages" value={count(data.sms)} />
          </div>
          <div className="actions">
            <button className="ghost" onClick={() => go(`path/${first}/${second}`)}>path</button>
            <button className="ghost" onClick={() => go(`graph/${first}`)}>graph</button>
          </div>
        </Panel>
      )}
    </div>
  )
}

export default function SubscriberScreen({ msisdn }: { msisdn?: string }) {
  const subscriber = useLoad<Subscriber | null>(
    (signal) => (msisdn ? api.subscriber(msisdn, signal) : Promise.resolve(null)),
    [msisdn],
  )
  const store = useLoad((signal) => api.totals(signal), [])

  if (!msisdn) {
    return (
      <div className="screen">
        <Panel title="look up">
          <Entry />
          <Suggest onPick={(id) => { remember('subscriber', id); go(`subscriber/${id}`) }} />
        </Panel>
      </div>
    )
  }

  const data = subscriber.data
  const totals = store.data
  const failures = data ? data['no-answer'] + data.busy + data.failed : 0
  const voice = data ? data['voice-out'] + data['voice-in'] : 0
  const storeRecords = totals?.records ?? 0
  const storeFailures = totals ? totals['noans-cnt'] + totals['busy-cnt'] + totals['failed-cnt'] : 0

  return (
    <div className="screen">
      <Panel title="look up"><Entry /></Panel>

      {subscriber.loading && !data && <Loading what={msisdn} />}
      {subscriber.error && (
        <>
          <Failure error={subscriber.error} what={msisdn} />
          <p className="quiet">
            a typo is the usual cause. the graph and a path both still take this number:{' '}
            <a href={`#/graph/${msisdn}`}>graph</a> · <a href={`#/path/${msisdn}/`}>path</a>
          </p>
        </>
      )}

      {data && (
        <>
          <Panel title={`subscriber ${data.msisdn}`} right={<button className="ghost" onClick={() => go(`graph/${data.msisdn}`)}>graph</button>}>
            <div className="tiles">
              <Tile label="voice out" value={duration(data['voice-out'])} />
              <Tile label="voice in" value={duration(data['voice-in'])} />
              <Tile label="sms out" value={count(data['sms-out'])} />
              <Tile label="sms in" value={count(data['sms-in'])} />
              <Tile label="data out" value={bytes(data['data-out'])} />
              <Tile label="data in" value={bytes(data['data-in'])} />
              <Tile label="no answer" value={count(data['no-answer'])} />
              <Tile label="busy" value={count(data.busy)} />
              <Tile label="failed" value={count(data.failed)} />
            </div>
          </Panel>

          <div className="two">
            <Panel title="out against in">
              <PairedBars
                unit="count"
                rows={[
                  { label: 'voice (s)', out: data['voice-out'], in: data['voice-in'] },
                  { label: 'sms', out: data['sms-out'], in: data['sms-in'] },
                  { label: 'data (B)', out: data['data-out'], in: data['data-in'] },
                ]}
              />
            </Panel>

            <Panel title="read against the store">
              <div className="tiles">
                <Tile label="failed attempts" value={count(failures)} note="no-answer, busy and failed" />
                <Tile
                  label="the store's failure share"
                  value={share(storeRecords ? storeFailures / storeRecords : null)}
                  note="every record processed, for scale"
                />
                <Tile label="voice seconds" value={duration(voice)} note="out and in together" />
              </div>
              <p className="quiet">
                a subscriber hash keeps call seconds, not call counts, so no average call length and
                no failure rate can be divided out here. the counts above are absolute.
              </p>
            </Panel>
          </div>

          <Panel title="peers">
            <Peers msisdn={data.msisdn} voice={voice} />
          </Panel>
        </>
      )}
    </div>
  )
}
