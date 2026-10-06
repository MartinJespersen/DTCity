param(
    [Parameter(Mandatory = $true)]
    [string]$build_directory,
    [int]$iterations = 3,
    [string[]]$sources = @(
        'src/utility/utm.cpp',
        'src/async/thread_pool.cpp',
        'src/render/vulkan/pipelines.cpp',
        'src/city/city.cpp',
        'src/cesium/cesium_tileset.cpp'
    )
)

$ErrorActionPreference = 'Stop'
if ($iterations -lt 1) { throw 'iterations must be positive' }
$project_directory = Split-Path $PSScriptRoot -Parent
$build_path = (Resolve-Path -LiteralPath $build_directory).Path
$log_path = Join-Path $build_path 'incremental-benchmark.log'

# Warm the build before timing edits; dependency installation and PCH creation
# should not be included in an implementation-only incremental measurement.
& cmake --build $build_path --target city *> $log_path
if ($LASTEXITCODE -ne 0) { throw "Initial build failed; see $log_path" }

$results = foreach ($source in $sources) {
    $source_path = Join-Path $project_directory $source
    $source_file = Get-Item -LiteralPath $source_path
    $original_write_time = $source_file.LastWriteTimeUtc
    try {
        for ($iteration = 1; $iteration -le $iterations; $iteration++) {
            # Change only the timestamp, leaving source contents untouched.
            $source_file.LastWriteTimeUtc = [DateTime]::UtcNow
            $timer = [System.Diagnostics.Stopwatch]::StartNew()
            & cmake --build $build_path --target city *> $log_path
            $timer.Stop()
            if ($LASTEXITCODE -ne 0) { throw "Build failed for $source; see $log_path" }
            $build_output = Get-Content -LiteralPath $log_path
            $compiled_objects = @($build_output | Select-String 'Building CXX object').Count
            if ($compiled_objects -ne 1) {
                throw "Expected one recompiled object for $source, got $compiled_objects; see $log_path"
            }
            [PSCustomObject]@{
                source = $source
                iteration = $iteration
                seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
                compiled_objects = $compiled_objects
            }
        }
    }
    finally {
        $source_file.LastWriteTimeUtc = $original_write_time
    }
}

$results | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $build_path 'incremental-benchmark.csv')
$results | Group-Object source | ForEach-Object {
    $sorted_times = @($_.Group.seconds | Sort-Object)
    $middle = [int][Math]::Floor($sorted_times.Count / 2)
    $median = $sorted_times[$middle]
    if ($sorted_times.Count % 2 -eq 0) {
        $median = ($sorted_times[$middle - 1] + $median) / 2
    }
    [PSCustomObject]@{ source = $_.Name; median_seconds = [Math]::Round($median, 3) }
} | Format-Table -AutoSize
