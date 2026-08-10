import { useCallback, useEffect, useState, useSyncExternalStore } from 'react'
import { ApiError } from './api'

export type Load<T> = {
  data: T | null
  error: ApiError | null
  loading: boolean
  at: number
  reload: () => void
}

/* One request, re-run when its deps change or the interval fires. */
export function useLoad<T>(
  run: (signal: AbortSignal) => Promise<T>,
  deps: unknown[],
  every = 0,
): Load<T> {
  const [data, setData] = useState<T | null>(null)
  const [error, setError] = useState<ApiError | null>(null)
  const [loading, setLoading] = useState(true)
  const [at, setAt] = useState(0)
  const [tick, setTick] = useState(0)

  const reload = useCallback(() => setTick((n) => n + 1), [])

  useEffect(() => {
    const controller = new AbortController()
    let alive = true
    setLoading(true)
    run(controller.signal)
      .then((answer) => {
        if (!alive) return
        setData(answer)
        setError(null)
        setAt(Date.now() / 1000)
      })
      .catch((failure) => {
        if (!alive || failure.name === 'AbortError') return
        setError(failure instanceof ApiError ? failure : new ApiError(0, { error: String(failure) }))
        setAt(Date.now() / 1000)
      })
      .finally(() => alive && setLoading(false))

    return () => {
      alive = false
      controller.abort()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [...deps, tick])

  useEffect(() => {
    if (!every) return
    const timer = setInterval(reload, every * 1000)
    return () => clearInterval(timer)
  }, [every, reload])

  return { data, error, loading, at, reload }
}

/* The current hash route, as its path segments. */
export function useRoute(): string[] {
  const hash = useSyncExternalStore(
    (listener) => {
      window.addEventListener('hashchange', listener)
      return () => window.removeEventListener('hashchange', listener)
    },
    () => window.location.hash,
  )
  return hash.replace(/^#\/?/, '').split('/').filter(Boolean)
}

export function go(route: string) {
  window.location.hash = route.startsWith('#') ? route : `#/${route}`
}

/* A value that ticks every second, for ages that have to keep moving. */
export function useNow(seconds = 1): number {
  const [now, setNow] = useState(Date.now() / 1000)
  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now() / 1000), seconds * 1000)
    return () => clearInterval(timer)
  }, [seconds])
  return now
}
