# Resolves contained paths and rejects linked ancestors before either source provenance or
# staged configuration is read. The original project is always treated as read-only.
function Get-SafePath([string]$Root, [string]$Relative) {
    $Root = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $Path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (!$Path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes expected root: $Path" }
    $Cursor = $Path
    while ($Cursor) {
        if ((Test-Path -LiteralPath $Cursor) -and ((Get-Item -LiteralPath $Cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse paths are not supported: $Cursor" }
        $Cursor = [IO.Path]::GetDirectoryName($Cursor)
    }
    return $Path
}

# Verifies that the executed project assembly was compiled from the current authoritative
# menu/level-catalog sources. Portable PDB document checksums provide byte-level provenance.
function Get-VerifiedProjectAssembly([string]$Stage, [string]$Source, [string]$ModuleId, [string[]]$RelativeSources, [string]$PublisherRoot, [string]$VerifiedAssemblyRoot = '') {
    if (![string]::IsNullOrWhiteSpace($VerifiedAssemblyRoot)) {
        $AssemblyPath = Get-SafePath $VerifiedAssemblyRoot ($ModuleId + '/' + $ModuleId + '.dll')
        if (!(Test-Path -LiteralPath $AssemblyPath -PathType Leaf)) { throw "Verified selector snapshot is missing: $AssemblyPath" }
        $Candidates = @(Get-Item -LiteralPath $AssemblyPath)
    } else {
        $Candidates = @(Get-ChildItem -LiteralPath (Join-Path $Stage 'cache') -Filter ($ModuleId + '.dll') -File -Recurse |
            Where-Object { $_.FullName.Replace('\', '/').EndsWith('/bin/' + $ModuleId + '/Debug/net9.0/' + $ModuleId + '.dll', [StringComparison]::Ordinal) } |
            Sort-Object LastWriteTimeUtc -Descending)
    }
    foreach ($Candidate in $Candidates) {
        $PdbPath = [IO.Path]::ChangeExtension($Candidate.FullName, '.pdb')
        if (!(Test-Path -LiteralPath $PdbPath -PathType Leaf)) { continue }
        $Stream = [IO.File]::OpenRead($PdbPath)
        $Provider = $null
        try {
            $Provider = [Reflection.Metadata.MetadataReaderProvider]::FromPortablePdbStream($Stream)
            $Reader = $Provider.GetMetadataReader()
            $Documents = @{}
            foreach ($Handle in $Reader.Documents) {
                $Document = $Reader.GetDocument($Handle)
                $Name = [IO.Path]::GetFileName($Reader.GetString($Document.Name))
                if (!$Documents.ContainsKey($Name)) { $Documents[$Name] = @() }
                $Documents[$Name] += [ordered]@{ algorithm = $Reader.GetGuid($Document.HashAlgorithm).ToString(); hash = [Convert]::ToHexString($Reader.GetBlobBytes($Document.Hash)) }
            }
            $SourceProof = @()
            $Matches = $true
            foreach ($Relative in $RelativeSources) {
                $OriginalPath = Get-SafePath $Source $Relative
                $StagePath = Get-SafePath $Stage $Relative
                $OriginalHash = (Get-FileHash -LiteralPath $OriginalPath -Algorithm SHA256).Hash
                if ($OriginalHash -ne (Get-FileHash -LiteralPath $StagePath -Algorithm SHA256).Hash) { throw "Authoritative selector source differs in stage: $Relative" }
                $Name = [IO.Path]::GetFileName($Relative)
                $Records = @($Documents[$Name])
                if ($Records.Count -ne 1 -or $Records[0].algorithm -ne '8829d00f-11b8-4213-878b-770e8597ac16' -or $Records[0].hash -ne $OriginalHash) {
                    $Matches = $false
                    break
                }
                $SourceProof += [ordered]@{ path = $OriginalPath; stagePath = $StagePath; sha256 = $OriginalHash; portablePdbDocumentHash = $Records[0].hash }
            }
            if (!$Matches) { continue }
            $CoreCopy = Join-Path $Candidate.DirectoryName 'helengine.core.dll'
            if (!(Test-Path -LiteralPath $CoreCopy) -or (Get-FileHash -LiteralPath $CoreCopy -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $PublisherRoot 'helengine.core.dll') -Algorithm SHA256).Hash) {
                throw "Project selector assembly was compiled against a different Core: $($Candidate.FullName)"
            }
            return [ordered]@{
                assembly = [Reflection.Assembly]::LoadFrom($Candidate.FullName)
                path = $Candidate.FullName
                sha256 = (Get-FileHash -LiteralPath $Candidate.FullName -Algorithm SHA256).Hash
                pdbPath = $PdbPath
                pdbSha256 = (Get-FileHash -LiteralPath $PdbPath -Algorithm SHA256).Hash
                sources = $SourceProof
            }
        } finally {
            if ($Provider) { $Provider.Dispose() }
            $Stream.Dispose()
        }
    }
    throw "No '$ModuleId' project assembly has verified current selector source checksums. Compile the managed project before scene selection."
}

# Computes the enabled LoadScene intersection with the physical catalog. Unavailable menu
# destinations remain in the definition; only the packaged scene selection is affected.
function Get-OnlineSceneClosure($MenuDefinition, [string[]]$AvailableSceneIds, [string]$StartupId, [string]$LoadingId, [string]$TiltSelectorId, [string[]]$TiltLevelIds) {
    $Available = [Collections.Generic.HashSet[string]]::new([string[]]$AvailableSceneIds, [StringComparer]::Ordinal)
    if (!$Available.Contains($StartupId) -or !$Available.Contains($LoadingId)) { throw 'Startup and loading scenes must exist in the physical catalog.' }
    $Selected = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $null = $Selected.Add($StartupId)
    $null = $Selected.Add($LoadingId)
    $Missing = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $MenuRows = @()
    $OnlineIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($Panel in $MenuDefinition.Panels) {
        foreach ($Item in $Panel.Items) {
            $MenuRows += [ordered]@{ panelId = $Panel.PanelId; itemId = $Item.ItemId; label = $Item.Label; enabled = $Item.Enabled; kind = $Item.Action.Kind.ToString(); targetId = $Item.Action.TargetId }
            if (!$Item.Enabled -or $Item.Action.Kind.ToString() -cne 'LoadScene') { continue }
            if ([string]::IsNullOrWhiteSpace($Item.Action.TargetId)) { throw "Enabled LoadScene item has no target: $($Item.ItemId)" }
            if ($Available.Contains($Item.Action.TargetId)) {
                $null = $Selected.Add($Item.Action.TargetId)
                $null = $OnlineIds.Add($Item.Action.TargetId)
            } else { $null = $Missing.Add($Item.Action.TargetId) }
        }
    }
    $InternalIds = @()
    if ($Selected.Contains($TiltSelectorId)) {
        foreach ($LevelId in $TiltLevelIds) {
            if (!$Available.Contains($LevelId)) { throw "Selected Tilt flow requires an absent authored level: $LevelId" }
            $null = $Selected.Add($LevelId)
            $InternalIds += $LevelId
        }
    }
    $Sorted = [Collections.Generic.List[string]]::new()
    $Sorted.AddRange([string[]]$Selected)
    $Sorted.Sort([StringComparer]::Ordinal)
    $Ordered = @($StartupId) + @($Sorted | Where-Object { $_ -cne $StartupId })
    return [ordered]@{
        selectedSceneIds = [string[]]$Ordered
        onlineSceneIds = [string[]]@($OnlineIds | Sort-Object -CaseSensitive)
        internalSceneIds = [string[]]$InternalIds
        missingOnlineSceneIds = [string[]]@($Missing | Sort-Object -CaseSensitive)
        excludedAuthoredSceneIds = [string[]]@($Available | Where-Object { !$Selected.Contains($_) } | Sort-Object -CaseSensitive)
        menuRows = $MenuRows
    }
}
