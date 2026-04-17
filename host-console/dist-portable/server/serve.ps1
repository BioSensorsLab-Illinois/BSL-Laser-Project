# BSL Host Console — portable static server (Windows).
#
# Serves the prebuilt React app (..\app\) at http://127.0.0.1:5173/
# and opens the default browser. Uses only PowerShell and .NET types
# that ship with Windows 10+ — no install needed.
#
# Usage (from run-windows.bat):
#   powershell -ExecutionPolicy Bypass -File serve.ps1
#   powershell -ExecutionPolicy Bypass -File serve.ps1 -Port 8080

param(
  [int]$Port = 5173,
  [string]$Root = ''
)

$ErrorActionPreference = 'Stop'

if (-not $Root) {
  $Root = Join-Path $PSScriptRoot '..\app'
}
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
  Write-Host "error: app directory not found: $Root" -ForegroundColor Red
  exit 1
}
$Root = (Resolve-Path -LiteralPath $Root).Path
# Ensure a trailing separator so path-prefix checks below can't match a
# sibling directory that happens to share the same prefix (e.g. C:\app
# vs C:\app-other\...).
$RootWithSep = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar

$mime = @{
  '.html'  = 'text/html; charset=utf-8'
  '.htm'   = 'text/html; charset=utf-8'
  '.js'    = 'application/javascript; charset=utf-8'
  '.mjs'   = 'application/javascript; charset=utf-8'
  '.css'   = 'text/css; charset=utf-8'
  '.json'  = 'application/json; charset=utf-8'
  '.svg'   = 'image/svg+xml'
  '.png'   = 'image/png'
  '.jpg'   = 'image/jpeg'
  '.jpeg'  = 'image/jpeg'
  '.gif'   = 'image/gif'
  '.ico'   = 'image/x-icon'
  '.woff'  = 'font/woff'
  '.woff2' = 'font/woff2'
  '.ttf'   = 'font/ttf'
  '.otf'   = 'font/otf'
  '.wasm'  = 'application/wasm'
  '.txt'   = 'text/plain; charset=utf-8'
  '.map'   = 'application/json'
}

$listener = New-Object System.Net.HttpListener
$prefix = "http://127.0.0.1:$Port/"
$listener.Prefixes.Add($prefix)

try {
  $listener.Start()
} catch {
  Write-Host "error: could not bind $prefix" -ForegroundColor Red
  Write-Host "  Another process may already be using that port." -ForegroundColor Red
  exit 2
}

Write-Host "BSL Console serving $Root"
Write-Host "  $prefix"
Write-Host "Press Ctrl+C to stop."

# Open default browser.
Start-Process $prefix | Out-Null

try {
  while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    $res = $ctx.Response

    try {
      $path = [Uri]::UnescapeDataString($req.Url.AbsolutePath)
      if ($path -eq '/' -or $path -eq '') { $path = '/index.html' }
      $rel  = $path.TrimStart('/')
      # Guard against path traversal — resolve and verify it stays under $Root.
      $file = [IO.Path]::GetFullPath((Join-Path $Root $rel))
      $underRoot = $file.StartsWith($RootWithSep, [StringComparison]::OrdinalIgnoreCase)

      if ($underRoot -and (Test-Path -LiteralPath $file -PathType Leaf)) {
        $ext = [IO.Path]::GetExtension($file).ToLower()
        $ct  = if ($mime.ContainsKey($ext)) { $mime[$ext] } else { 'application/octet-stream' }
        $bytes = [IO.File]::ReadAllBytes($file)
        $res.ContentType     = $ct
        $res.ContentLength64 = $bytes.Length
        $res.OutputStream.Write($bytes, 0, $bytes.Length)
      } else {
        $res.StatusCode = 404
        $msg = [Text.Encoding]::UTF8.GetBytes("404 Not Found: $path")
        $res.ContentType     = 'text/plain; charset=utf-8'
        $res.ContentLength64 = $msg.Length
        $res.OutputStream.Write($msg, 0, $msg.Length)
      }
    } catch {
      $res.StatusCode = 500
      $msg = [Text.Encoding]::UTF8.GetBytes("500 Internal Server Error")
      $res.ContentType     = 'text/plain; charset=utf-8'
      $res.ContentLength64 = $msg.Length
      $res.OutputStream.Write($msg, 0, $msg.Length)
    } finally {
      $res.Close()
    }
  }
}
finally {
  if ($listener.IsListening) { $listener.Stop() }
  $listener.Close()
}
