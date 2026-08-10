import { useState } from 'react'
import { api, type Operator } from '../lib/api'
import { count, duration, share } from '../lib/format'
import { go, useLoad } from '../lib/hooks'
import { remember } from '../lib/history'
import { PairedBars } from '../components/charts'
import { Suggest } from '../components/Suggest'
import { Failure, Loading, Panel, SearchIcon, Tile } from '../components/parts'

export default function OperatorScreen({ mccmnc }: { mccmnc?: string }) {
  const [typed, setTyped] = useState('')

  const operator = useLoad<Operator | null>(
    (signal) => (mccmnc ? api.operator(mccmnc, signal) : Promise.resolve(null)),
    [mccmnc],
  )
  const store = useLoad((signal) => api.totals(signal), [])

  const submit = (event: React.FormEvent) => {
    event.preventDefault()
    const code = typed.match(/\d+/g)?.[0]
    if (!code) return
    remember('operator', code)
    go(`operator/${code}`)
  }

  const data = operator.data
  const totals = store.data
  const storeVoice = totals ? totals['moc-dur'] + totals['mtc-dur'] : 0
  const voice = data ? data['voice-out'] + data['voice-in'] : 0

  return (
    <div className="screen">
      <Panel title="look up">
        <form className="entry" onSubmit={submit}>
          <div className="entry-row">
            <input inputMode="numeric" placeholder="mccmnc" value={typed}
                   onChange={(event) => setTyped(event.target.value)} />
            <button type="submit" aria-label="search" title="search"><SearchIcon /></button>
          </div>
        </form>

        <Suggest board="op-voice" onPick={(id) => { remember('operator', id); go(`operator/${id}`) }} />
      </Panel>

      {operator.loading && mccmnc && !data && <Loading what={mccmnc} />}
      {operator.error && <Failure error={operator.error} what={mccmnc ?? 'that operator'} />}

      {data && (
        <>
          <Panel title={`operator ${data.mccmnc}`}>
            <div className="tiles">
              <Tile label="voice out" value={duration(data['voice-out'])} />
              <Tile label="voice in" value={duration(data['voice-in'])} />
              <Tile label="sms out" value={count(data['sms-out'])} />
              <Tile label="sms in" value={count(data['sms-in'])} />
            </div>
          </Panel>

          <div className="two">
            <Panel title="out against in">
              <PairedBars
                unit="count"
                rows={[
                  { label: 'voice (s)', out: data['voice-out'], in: data['voice-in'] },
                  { label: 'sms', out: data['sms-out'], in: data['sms-in'] },
                ]}
              />
            </Panel>
            <Panel title="share of the store">
              <div className="tiles">
                <Tile label="of all call seconds" value={share(storeVoice ? voice / storeVoice : null)}
                      note="moc-dur + mtc-dur across the store" />
                <Tile label="voice seconds" value={duration(voice)} />
              </div>
            </Panel>
          </div>
        </>
      )}
    </div>
  )
}
