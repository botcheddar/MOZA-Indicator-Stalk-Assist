param([string]$Destination = (Join-Path $PSScriptRoot '..\release\moza_turn_signal_assist.zip'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\src\beamng'))
$archive = [IO.Compression.ZipArchive]::new([IO.File]::Open($Destination,[IO.FileMode]::Create),[IO.Compression.ZipArchiveMode]::Create)
try {
 foreach ($file in Get-ChildItem -LiteralPath $root -File -Recurse) {
  $name=$file.FullName.Substring($root.Length+1).Replace('\','/')
  $entry=$archive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
  $stream=$entry.Open();$input=[IO.File]::OpenRead($file.FullName)
  try{$input.CopyTo($stream)}finally{$input.Dispose();$stream.Dispose()}
 }
} finally {$archive.Dispose()}
Write-Output "Created $Destination"
