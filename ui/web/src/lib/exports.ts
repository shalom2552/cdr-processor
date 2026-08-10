function download(name: string, blob: Blob) {
  const url = URL.createObjectURL(blob)
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = name
  anchor.click()
  URL.revokeObjectURL(url)
}

export function toCsv(name: string, rows: (string | number)[][]) {
  const text = rows
    .map((row) => row.map((cell) => (/[",\n]/.test(String(cell)) ? `"${String(cell).replace(/"/g, '""')}"` : cell)).join(','))
    .join('\n')
  download(name, new Blob([text], { type: 'text/csv' }))
}

export function toJson(name: string, data: unknown) {
  download(name, new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }))
}

export function svgToPng(svg: SVGSVGElement, name: string) {
  const box = svg.getBoundingClientRect()
  const source = new XMLSerializer().serializeToString(svg)
  const image = new Image()
  image.onload = () => {
    const canvas = document.createElement('canvas')
    canvas.width = box.width * 2
    canvas.height = box.height * 2
    const context = canvas.getContext('2d')!
    context.fillStyle = getComputedStyle(document.body).backgroundColor
    context.fillRect(0, 0, canvas.width, canvas.height)
    context.drawImage(image, 0, 0, canvas.width, canvas.height)
    canvas.toBlob((blob) => blob && download(name, blob))
  }
  image.src = 'data:image/svg+xml;base64,' + btoa(unescape(encodeURIComponent(source)))
}

export function canvasToPng(canvas: HTMLCanvasElement, name: string) {
  canvas.toBlob((blob) => blob && download(name, blob))
}
