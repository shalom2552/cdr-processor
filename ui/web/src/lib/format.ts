export function count(value: number | null | undefined): string {
  if (value === null || value === undefined) return '—'
  return value.toLocaleString('en-US', { maximumFractionDigits: 0 })
}

export function decimal(value: number | null | undefined, places = 2): string {
  if (value === null || value === undefined) return '—'
  return value.toLocaleString('en-US', { maximumFractionDigits: places })
}

export function duration(seconds: number | null | undefined): string {
  if (seconds === null || seconds === undefined) return '—'
  const whole = Math.floor(seconds)
  const hours = Math.floor(whole / 3600)
  const minutes = Math.floor((whole % 3600) / 60)
  const rest = whole % 60
  if (hours) return `${count(hours)}h ${minutes}m ${rest}s`
  if (minutes) return `${minutes}m ${rest}s`
  return `${rest}s`
}

const UNITS = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']

export function bytes(value: number | null | undefined): string {
  if (value === null || value === undefined) return '—'
  let size = value
  let unit = 0
  while (size >= 1024 && unit < UNITS.length - 1) {
    size /= 1024
    unit += 1
  }
  return `${unit === 0 ? size : size.toFixed(size < 10 ? 2 : 1)} ${UNITS[unit]}`
}

export function share(value: number | null | undefined): string {
  if (value === null || value === undefined || Number.isNaN(value)) return '—'
  return `${(value * 100).toFixed(value < 0.01 ? 2 : 1)}%`
}

export function rate(value: number | null | undefined, unit = '/s'): string {
  if (value === null || value === undefined) return '—'
  return `${decimal(value, value < 10 ? 2 : 0)}${unit}`
}

export function clock(ts: number | null | undefined): string {
  if (!ts) return '—'
  return new Date(ts * 1000).toLocaleTimeString()
}

export function ago(ts: number | null | undefined): string {
  if (!ts) return 'never'
  const seconds = Math.max(0, Math.floor(Date.now() / 1000 - ts))
  if (seconds < 60) return `${seconds}s ago`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`
  return `${Math.floor(seconds / 86400)}d ago`
}

export type Unit = 'count' | 'seconds' | 'bytes' | 'share' | 'rate'

export function value(amount: number | null | undefined, unit: Unit): string {
  switch (unit) {
    case 'seconds':
      return duration(amount)
    case 'bytes':
      return bytes(amount)
    case 'share':
      return share(amount)
    case 'rate':
      return rate(amount)
    default:
      return count(amount)
  }
}
