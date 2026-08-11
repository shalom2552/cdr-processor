import { useSyncExternalStore } from 'react'

export type Lookup = { kind: 'subscriber' | 'operator' | 'link' | 'path' | 'graph'; id: string; at: number }

const KEY = 'cdr-insight.history'
const LIMIT = 25

function read(): Lookup[] {
  try {
    return JSON.parse(localStorage.getItem(KEY) ?? '[]')
  } catch {
    return []
  }
}

let current = read()
const listeners = new Set<() => void>()

function publish() {
  localStorage.setItem(KEY, JSON.stringify(current))
  listeners.forEach((listener) => listener())
}

export function remember(kind: Lookup['kind'], id: string) {
  current = [{ kind, id, at: Date.now() / 1000 }, ...current.filter((seen) => !(seen.kind === kind && seen.id === id))].slice(0, LIMIT)
  publish()
}

export function forget() {
  current = []
  publish()
}

export function useHistory(): Lookup[] {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
    () => current,
  )
}

export function routeOf(lookup: Lookup): string {
  switch (lookup.kind) {
    case 'subscriber':
      return `#/subscriber/${lookup.id}`
    case 'operator':
      return `#/operator/${lookup.id}`
    case 'link':
      return `#/subscriber/${lookup.id.split('/')[0]}`
    case 'path':
      return `#/path/${lookup.id}`
    default:
      return `#/graph/${lookup.id}`
  }
}
