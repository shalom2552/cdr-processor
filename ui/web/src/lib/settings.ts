import { useSyncExternalStore } from 'react'

export type Settings = {
  theme: 'system' | 'dark' | 'light'
  refresh: number
  window: string
  peerPage: number
  topPage: number
  expandLimit: number
  maxNodes: number
  edgeMetric: 'duration' | 'sms'
}

export const DEFAULTS: Settings = {
  theme: 'system',
  refresh: 5,
  window: '1h',
  peerPage: 100,
  topPage: 20,
  expandLimit: 50,
  maxNodes: 500,
  edgeMetric: 'duration',
}

const KEY = 'cdr-insight.settings'

function read(): Settings {
  try {
    return { ...DEFAULTS, ...JSON.parse(localStorage.getItem(KEY) ?? '{}') }
  } catch {
    return { ...DEFAULTS }
  }
}

let current = read()
const listeners = new Set<() => void>()

function publish() {
  localStorage.setItem(KEY, JSON.stringify(current))
  document.documentElement.dataset.theme = current.theme
  listeners.forEach((listener) => listener())
}

export function setSettings(patch: Partial<Settings>) {
  current = { ...current, ...patch }
  publish()
}

export function resetSettings() {
  current = { ...DEFAULTS }
  publish()
}

export function getSettings(): Settings {
  return current
}

export function applyTheme() {
  document.documentElement.dataset.theme = current.theme
}

export function useSettings(): Settings {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
    () => current,
  )
}
