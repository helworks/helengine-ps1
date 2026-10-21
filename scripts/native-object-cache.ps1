function Reset-NativeCompilerOutputs {
    <# Removes only derived compiler files before restoring the cache for the selected flags. #>
    param([Parameter(Mandatory=$true)][string]$Root)
    if(![IO.Path]::IsPathRooted($Root)){throw 'Native compiler output root must be absolute.'}
    $resolved=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    if($resolved -eq [IO.Path]::GetPathRoot($resolved).TrimEnd('\','/')){throw 'Native compiler output root cannot be a volume root.'}
    $directory=Get-Item -LiteralPath $resolved -Force -ErrorAction Stop
    if(!$directory.PSIsContainer){throw 'Native compiler output root must be a directory.'}
    $items=@($directory)+@(Get-ChildItem -LiteralPath $resolved -Recurse -Force -ErrorAction Stop)
    if(@($items | Where-Object {($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0}).Count -ne 0){
        throw 'Native compiler output root must not contain reparse points.'
    }
    $files=@($items | Where-Object {!$_.PSIsContainer -and $_.Extension -in @('.o','.dep','.a')})
    foreach($file in $files){
        $target=[IO.Path]::GetFullPath($file.FullName)
        if(!$target.StartsWith($resolved+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Compiler output escapes the selected root.'}
    }
    foreach($file in $files){Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop}
    Write-Host "Reset native compiler outputs: $($files.Count) files"
}
