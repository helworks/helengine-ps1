"""Emit native build tables from the partition plan and actual HEP1 archive, never guessed offsets."""
import argparse
import hashlib
import json
import pathlib
import struct


DECLARATION_HEADER = '#pragma once\n#include "platform/ps1/modules/Ps1SceneModuleMetadata.hpp"\n'


def cstring(value):
    return json.dumps(str(value), ensure_ascii=True)


def digest_initializer(value):
    raw = bytes.fromhex(value)
    if len(raw) != 32:
        raise ValueError("SHA256 must have 32 bytes")
    return "{" + ",".join(str(byte) for byte in raw) + "}"


def archive_entries(image):
    if len(image)<16 or image[:4]!=b"HEP1":
        raise ValueError("Expected actual HEP1 archive")
    version,count,total=struct.unpack_from("<III",image,4)
    if version!=1 or total!=len(image) or count>(len(image)-16)//16:
        raise ValueError("Invalid archive header")
    result={}
    table_end=16+count*16
    for index in range(count):
        path_offset,path_bytes,data_offset,data_bytes=struct.unpack_from("<IIII",image,16+index*16)
        if min(path_offset,data_offset)<table_end or path_offset+path_bytes>len(image) or data_offset+data_bytes>len(image) or data_offset%4:
            raise ValueError("Invalid archive entry range")
        name=image[path_offset:path_offset+path_bytes].decode("utf-8")
        if name in result:
            raise ValueError("Duplicate archive asset path")
        result[name]=(data_offset,data_bytes,hashlib.sha256(image[data_offset:data_offset+data_bytes]).hexdigest())
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan",type=pathlib.Path)
    parser.add_argument("--archive",type=pathlib.Path)
    parser.add_argument("--output",required=True,type=pathlib.Path)
    parser.add_argument("--cache-budget-bytes",type=int)
    parser.add_argument("--declarations-only",action="store_true")
    parser.add_argument("--module-receipts",type=pathlib.Path)
    args=parser.parse_args()
    if args.declarations_only:
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/"ps1_scene_module_metadata.hpp").write_text(DECLARATION_HEADER,encoding="utf-8")
        return
    if args.plan is None or args.archive is None or args.cache_budget_bytes is None:
        parser.error("Table emission requires --plan, --archive and --cache-budget-bytes")
    plan=json.loads(args.plan.read_text(encoding="utf-8-sig"))
    if plan["schemaVersion"]!=1 or args.cache_budget_bytes<=0:
        raise ValueError("Unsupported partition schema or missing cache budget")
    # A persistent loading scene's bytes remain leased while any target is materialized.
    persistent=set(plan.get("persistentAssetPaths", []))
    for scene in plan["scenes"]:
        scene["assetPaths"]=sorted(set(scene["assetPaths"]) | persistent)
    image=args.archive.read_bytes();entries=archive_entries(image)
    planned={entry["relativePath"]:entry for entry in plan["assets"]}
    if set(entries)!=set(planned):
        raise ValueError("Archive assets differ from the official partition dependency table")
    for name,(_,length,digest) in entries.items():
        expected=planned[name]
        if length!=expected["byteLength"] or digest.lower()!=expected["sha256"].lower():
            raise ValueError("Archive asset content differs from plan: "+name)
    names=sorted(entries)
    if len(names)>65535:
        raise ValueError("Too many assets for the bounded resident index")
    indices={name:index for index,name in enumerate(names)}
    modules=plan["modules"]
    slots={slot["index"]:slot for slot in plan["slots"]}
    if any("loadAddress" not in slot or "capacityBytes" not in slot for slot in slots.values()):
        raise ValueError("Arena layout must be measured before metadata emission")
    cpp=['#include "ps1_scene_module_metadata.hpp"', '#include "RuntimeCodeModuleManifest.hpp"',
         '#include "RuntimeCodeModuleManifestEntry.hpp"','#include "RuntimeCodeModuleLoadState.hpp"',
         '#include "platform/ps1/Ps1RuntimeProvider.hpp"',
         'namespace helengine::ps1 { namespace {']
    for index,module in enumerate(modules):
        slot=slots[module["slot"]]
        receipt_path=None if args.module_receipts is None else args.module_receipts/("MOD"+str(index).zfill(5)+".json")
        receipt=None if receipt_path is None else json.loads(receipt_path.read_text(encoding="utf-8"))
        size=128 if receipt is None else receipt["fileBytes"]
        build_id="00"*32 if receipt is None else receipt["residentBuildId"]
        file_digest="00"*32 if receipt is None else receipt["fileSha256"]
        if receipt is not None and (receipt["loadAddress"]!=slot["loadAddress"] or receipt["arenaBytes"]!=slot["capacityBytes"]):
            raise ValueError("Module receipt does not match measured slot")
        cpp.append('const Ps1ModuleDescriptor Module'+str(index)+' = {'+cstring(module["moduleId"])+','+
                   cstring("MOD"+str(index).zfill(5)+".BIN;1")+','+str(size)+','+str(slot["loadAddress"])+','+
                   str(slot["capacityBytes"])+','+digest_initializer(build_id)+','+digest_initializer(file_digest)+'};')
    cpp.append('const Ps1DiscModuleBinding Modules[] = {'+','.join('{&Module'+str(i)+','+str(module["slot"])+'}' for i,module in enumerate(modules))+'};')
    cpp.append('Ps1CodeModulePinState Pins[] = {'+','.join('{'+cstring(module["moduleId"])+',0,false}' for module in modules)+'};')
    cpp.append('const Ps1DiscAssetDescriptor Assets[] = {'+','.join('{'+cstring(name)+','+str(entries[name][0])+','+str(entries[name][1])+','+digest_initializer(entries[name][2])+'}' for name in names)+'};')
    for index,scene in enumerate(plan["scenes"]):
        dependencies=sorted(set(scene["assetPaths"]))
        if len(dependencies)!=len(scene["assetPaths"]) or scene["cookedRelativePath"] not in dependencies:
            raise ValueError("Scene dependencies must be deduplicated and include scene payload")
        cpp.append('const std::uint16_t SceneAssets'+str(index)+'[] = {'+','.join(str(indices[name]) for name in dependencies)+'};')
    cpp.append('const Ps1DiscSceneDependencies Scenes[] = {'+','.join('{'+cstring(scene["sceneId"])+',SceneAssets'+str(index)+','+str(len(scene["assetPaths"]))+'}' for index,scene in enumerate(plan["scenes"]))+'};')
    cpp.append('} // namespace')
    cpp.extend(['const Ps1DiscModuleBinding* Ps1GeneratedModules(std::size_t* count) { *count='+str(len(modules))+';return Modules; }',
                'Ps1CodeModulePinState* Ps1GeneratedPins(std::size_t* count) { *count='+str(len(modules))+';return Pins; }',
                'const Ps1DiscAssetDescriptor* Ps1GeneratedAssets(std::uint32_t* count) { *count='+str(len(names))+';return Assets; }',
                'const Ps1DiscSceneDependencies* Ps1GeneratedScenes(std::uint32_t* count) { *count='+str(len(plan["scenes"]))+';return Scenes; }',
                'std::uint32_t Ps1GeneratedArchiveBytes() { return '+str(len(image))+'; }',
                'std::uint32_t Ps1GeneratedCacheBudgetBytes() { return '+str(args.cache_budget_bytes)+'; }'])
    cpp.append('Array<HeCppString>* Ps1GeneratedRequiredModules(const char* sceneId) {')
    for scene in plan["scenes"]:
        ids=scene["requiredCodeModuleIds"]
        cpp.append('if (__builtin_strcmp(sceneId,'+cstring(scene["sceneId"])+')==0) { auto* ids=new Array<HeCppString>('+str(len(ids))+');')
        for i,module_id in enumerate(ids): cpp.append('(*ids)['+str(i)+']='+cstring(module_id)+';')
        cpp.append('return ids; }')
    cpp.append('he_cpp_custom::Fail("Scene missing native module dependency entry"); }')
    cpp.append('::RuntimeCodeModuleManifest* Ps1GeneratedModuleManifest() { auto* entries=new Array<::RuntimeCodeModuleManifestEntry*>('+str(len(modules))+');')
    for index,module in enumerate(modules):
        dependencies=module["dependencyModuleIds"]
        cpp.append('auto* deps'+str(index)+'=new Array<HeCppString>('+str(len(dependencies))+');')
        for i,value in enumerate(dependencies): cpp.append('(*deps'+str(index)+')['+str(i)+']='+cstring(value)+';')
        cpp.append('(*entries)['+str(index)+']=Ps1CreateDiscModuleEntry('+cstring(module["moduleId"])+',deps'+str(index)+');')
    cpp.append('return Ps1CreateDiscModuleManifest(entries); }\n} // namespace helengine::ps1')
    header=DECLARATION_HEADER
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/"ps1_scene_module_metadata.hpp").write_text(header,encoding="utf-8")
    (args.output/"ps1_scene_module_metadata.cpp").write_text("\n".join(cpp)+"\n",encoding="utf-8")
    (args.output/"metadata-receipt.json").write_text(json.dumps({"schemaVersion":1,"moduleCount":len(modules),
        "sceneCount":len(plan["scenes"]),"assetCount":len(names),"archiveBytes":len(image),
        "archiveSha256":hashlib.sha256(image).hexdigest(),"cacheBudgetBytes":args.cache_budget_bytes,
        "placeholderModuleDigests":args.module_receipts is None},indent=2)+"\n",encoding="utf-8")


if __name__ == "__main__":
    main()
