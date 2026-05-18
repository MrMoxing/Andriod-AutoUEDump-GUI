#include "AutoFixOffsets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include "../UE/UEMemory.hpp"
#include "../UE/UEGameProfile.hpp"
#include "../UE/UEWrappers.hpp"
#include "../Utils/Logger.hpp"

using namespace UEMemory;

namespace AutoFix
{
    namespace
    {
        constexpr uintptr_t kNameScanStart = 0x08;
        constexpr uintptr_t kNameScanEnd = 0x7C;
        constexpr int kStrictSampleCap = 0x40;
        constexpr int kLooseSampleCap = 0x400;

        UE_Offsets *Offsets() { return UEWrappers::GetOffsets(); }
        const UEVars *Vars() { return UEWrappers::GetUEVars(); }
        UE_UObjectArray *Objects() { return UEWrappers::GetObjects(); }

        uintptr_t BaseAddress()
        {
            return UEWrappers::GetBaseAddress();
        }

        uintptr_t ReadPtr(uintptr_t address)
        {
            return vm_rpm_ptr<uintptr_t>((const void *)address);
        }

        int32_t ReadI32(uintptr_t address)
        {
            return vm_rpm_ptr<int32_t>((const void *)address);
        }

        std::string ReadNameById(int32_t id)
        {
            if (id <= 0 || id > 0x800000)
                return "";
            return UEWrappers::GetNameByID(id);
        }

        std::string ReadNameAt(uintptr_t address)
        {
            return ReadNameById(ReadI32(address));
        }

        std::string ReadObjectName(uintptr_t object, uintptr_t nameOffset)
        {
            if (object == 0 || nameOffset == 0)
                return "";
            return ReadNameAt(object + nameOffset);
        }

        bool IsLikelyPtr(uintptr_t p)
        {
            return p >= 0x10000 && kPtrValidator.isPtrReadable((const void *)p);
        }

        bool IsExecutableInModule(uintptr_t p)
        {
            const uintptr_t base = BaseAddress();
            if (!base || p <= base)
                return false;

            const uintptr_t rva = p - base;
            return rva < 0x30000000 && kPtrValidator.isPtrExecutable((const void *)p);
        }

        bool StartsWith(const std::string &value, const char *prefix)
        {
            const std::string p = prefix ? prefix : "";
            return value.size() >= p.size() && value.compare(0, p.size(), p) == 0;
        }

        bool IsIdentifierLike(const std::string &name)
        {
            if (name.size() < 2 || name.size() > 128)
                return false;
            if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')
                return false;

            for (unsigned char c : name)
            {
                if (!(std::isalnum(c) || c == '_'))
                    return false;
            }
            return name != "None" && name != "NULL";
        }

        bool IsUELikeName(const std::string &name)
        {
            if (name.empty())
                return false;
            if (StartsWith(name, "/Script/"))
                return true;
            if (name.find('.') != std::string::npos)
                return true;
            return IsIdentifierLike(name);
        }

        void ApplyDefaultsOnly()
        {
            UE_Offsets *off = Offsets();
            if (!off)
                return;

            if (off->FName.Size == 0)
            {
                off->FName.Size = UE_DefaultOffsets::kGetFNameSize(off->Config.isUsingCasePreservingName,
                                                                   off->Config.isUsingOutlineNumberName);
            }

            if (off->FUObjectArray.ObjObjects == 0)
                off->FUObjectArray.ObjObjects = sizeof(int32_t) * 4;
            if (off->TUObjectArray.Objects == 0)
                off->TUObjectArray.Objects = 0;
            if (off->TUObjectArray.NumElements == 0)
            {
                off->TUObjectArray.NumElements = (off->TUObjectArray.NumElementsPerChunk > 0)
                    ? ((sizeof(void *) * 2) + sizeof(int32_t))
                    : (sizeof(void *) + sizeof(int32_t));
            }
            if (off->FUObjectItem.Object == 0)
                off->FUObjectItem.Object = 0;
            if (off->FUObjectItem.Size == 0)
                off->FUObjectItem.Size = GetPtrAlignedOf(sizeof(void *) + (sizeof(int32_t) * 3));
        }

        void PrintField(const char *label, uintptr_t before, uintptr_t after)
        {
            if (before == after)
                LOGI("[AutoFix]   %-32s = 0x%lx", label, (unsigned long)after);
            else
                LOGI("[AutoFix]   %-32s = 0x%lx  (was 0x%lx)", label, (unsigned long)after, (unsigned long)before);
        }

        void DumpAutoFoundOffsets(const UE_Offsets &before, const UE_Offsets &after)
        {
            LOGI("[AutoFix] ============== Auto-Found Offsets ==============");
            PrintField("FUObjectArray.ObjObjects", before.FUObjectArray.ObjObjects, after.FUObjectArray.ObjObjects);
            PrintField("TUObjectArray.Objects", before.TUObjectArray.Objects, after.TUObjectArray.Objects);
            PrintField("TUObjectArray.NumElements", before.TUObjectArray.NumElements, after.TUObjectArray.NumElements);
            PrintField("TUObjectArray.NumElementsPerChunk", before.TUObjectArray.NumElementsPerChunk, after.TUObjectArray.NumElementsPerChunk);
            PrintField("FUObjectItem.Object", before.FUObjectItem.Object, after.FUObjectItem.Object);
            PrintField("FUObjectItem.Size", before.FUObjectItem.Size, after.FUObjectItem.Size);
            PrintField("UObject.ObjectFlags", before.UObject.ObjectFlags, after.UObject.ObjectFlags);
            PrintField("UObject.InternalIndex", before.UObject.InternalIndex, after.UObject.InternalIndex);
            PrintField("UObject.ClassPrivate", before.UObject.ClassPrivate, after.UObject.ClassPrivate);
            PrintField("UObject.NamePrivate", before.UObject.NamePrivate, after.UObject.NamePrivate);
            PrintField("UObject.OuterPrivate", before.UObject.OuterPrivate, after.UObject.OuterPrivate);
            PrintField("UField.Next", before.UField.Next, after.UField.Next);
            PrintField("UEnum.Names", before.UEnum.Names, after.UEnum.Names);
            PrintField("UStruct.SuperStruct", before.UStruct.SuperStruct, after.UStruct.SuperStruct);
            PrintField("UStruct.Children", before.UStruct.Children, after.UStruct.Children);
            PrintField("UStruct.ChildProperties", before.UStruct.ChildProperties, after.UStruct.ChildProperties);
            PrintField("UStruct.PropertiesSize", before.UStruct.PropertiesSize, after.UStruct.PropertiesSize);
            PrintField("UFunction.EFunctionFlags", before.UFunction.EFunctionFlags, after.UFunction.EFunctionFlags);
            PrintField("UFunction.NumParams", before.UFunction.NumParams, after.UFunction.NumParams);
            PrintField("UFunction.ParamSize", before.UFunction.ParamSize, after.UFunction.ParamSize);
            PrintField("UFunction.Func", before.UFunction.Func, after.UFunction.Func);
            PrintField("UProperty.ArrayDim", before.UProperty.ArrayDim, after.UProperty.ArrayDim);
            PrintField("UProperty.ElementSize", before.UProperty.ElementSize, after.UProperty.ElementSize);
            PrintField("UProperty.PropertyFlags", before.UProperty.PropertyFlags, after.UProperty.PropertyFlags);
            PrintField("UProperty.Offset_Internal", before.UProperty.Offset_Internal, after.UProperty.Offset_Internal);
            PrintField("UProperty.Size", before.UProperty.Size, after.UProperty.Size);
            PrintField("FField.ClassPrivate", before.FField.ClassPrivate, after.FField.ClassPrivate);
            PrintField("FField.Next", before.FField.Next, after.FField.Next);
            PrintField("FField.NamePrivate", before.FField.NamePrivate, after.FField.NamePrivate);
            PrintField("FField.FlagsPrivate", before.FField.FlagsPrivate, after.FField.FlagsPrivate);
            PrintField("FProperty.ArrayDim", before.FProperty.ArrayDim, after.FProperty.ArrayDim);
            PrintField("FProperty.ElementSize", before.FProperty.ElementSize, after.FProperty.ElementSize);
            PrintField("FProperty.PropertyFlags", before.FProperty.PropertyFlags, after.FProperty.PropertyFlags);
            PrintField("FProperty.Offset_Internal", before.FProperty.Offset_Internal, after.FProperty.Offset_Internal);
            PrintField("FProperty.Size", before.FProperty.Size, after.FProperty.Size);
            LOGI("[AutoFix] ================================================");
        }

        void DumpObjectNameCandidates(int32_t idx, uintptr_t object)
        {
            const int32_t id18 = ReadI32(object + 0x18);
            const int32_t id20 = ReadI32(object + 0x20);
            const int32_t id28 = ReadI32(object + 0x28);

            const std::string n18 = ReadNameById(id18);
            const std::string n20 = ReadNameById(id20);
            const std::string n28 = ReadNameById(id28);

            LOGW("[AutoFix] Dump idx=%d obj=%p id@0x18=%d('%s') id@0x20=%d('%s') id@0x28=%d('%s')",
                 idx,
                 (void *)object,
                 id18,
                 n18.c_str(),
                 id20,
                 n20.c_str(),
                 id28,
                 n28.c_str());
        }

        bool ValidateUObjectLayout(uintptr_t nameOffset, int sampleCap, int *decodedOut = nullptr, int *totalOut = nullptr)
        {
            if (!Objects() || nameOffset == 0)
                return false;

            const int32_t total = Objects()->GetNumElements();
            const int32_t limit = std::min(total, sampleCap);

            int decoded = 0;
            int scriptAnchors = 0;

            for (int32_t i = 0; i < limit; ++i)
            {
                const uintptr_t object = reinterpret_cast<uintptr_t>(Objects()->GetObjectPtr(i));
                if (!IsLikelyPtr(object))
                    continue;

                const std::string name = ReadObjectName(object, nameOffset);
                if (name.empty())
                    continue;

                if (IsUELikeName(name))
                    ++decoded;
                if (StartsWith(name, "/Script/"))
                    ++scriptAnchors;
            }

            if (decodedOut)
                *decodedOut = decoded;
            if (totalOut)
                *totalOut = limit;

            if (scriptAnchors >= 16)
            {
                LOGI("[AutoFix] UObject layout OK: %d/%d objects decode to UE-like names @ NamePrivate=0x%lx",
                     decoded,
                     limit,
                     (unsigned long)nameOffset);
                return true;
            }

            return false;
        }

        bool FixupNamePrivate()
        {
            UE_Offsets *off = Offsets();
            if (!off || !Objects())
                return false;

            const uintptr_t before = off->UObject.NamePrivate;
            if (before)
            {
                int decoded = 0;
                int total = 0;
                if (ValidateUObjectLayout(before, kLooseSampleCap, &decoded, &total))
                    return true;

                LOGW("[AutoFix] No /Script/* anchor found at preset NamePrivate=0x%lx (decoded=%d)",
                     (unsigned long)before,
                     decoded);
            }

            const int32_t total = Objects()->GetNumElements();
            const int32_t strictLimit = std::min(total, kStrictSampleCap);

            for (int32_t i = 0; i < strictLimit; ++i)
            {
                const uintptr_t object = reinterpret_cast<uintptr_t>(Objects()->GetObjectPtr(i));
                if (!IsLikelyPtr(object))
                    continue;

                for (uintptr_t candidate = kNameScanStart; candidate <= kNameScanEnd; candidate += 0x4)
                {
                    if (ReadObjectName(object, candidate) == "/Script/CoreUObject")
                    {
                        LOGI("[AutoFix] BruteForce NamePrivate=0x%lx (was 0x%lx) anchor='/Script/CoreUObject' @ idx=%d",
                             (unsigned long)candidate,
                             (unsigned long)before,
                             i);
                        off->UObject.NamePrivate = candidate;
                        return true;
                    }
                }
            }

            uintptr_t bestCandidate = 0;
            int bestScore = -1;
            int bestIndex = -1;
            std::string bestAnchor;

            const int32_t looseLimit = std::min(total, kLooseSampleCap);
            for (int32_t i = 0; i < looseLimit; ++i)
            {
                const uintptr_t object = reinterpret_cast<uintptr_t>(Objects()->GetObjectPtr(i));
                if (!IsLikelyPtr(object))
                    continue;

                for (uintptr_t candidate = kNameScanStart; candidate <= kNameScanEnd; candidate += 0x4)
                {
                    const std::string name = ReadObjectName(object, candidate);
                    if (!StartsWith(name, "/Script/"))
                        continue;

                    int decoded = 0;
                    int totalChecked = 0;
                    if (!ValidateUObjectLayout(candidate, kLooseSampleCap, &decoded, &totalChecked))
                        continue;

                    if (decoded > bestScore)
                    {
                        bestScore = decoded;
                        bestCandidate = candidate;
                        bestIndex = i;
                        bestAnchor = name;
                    }
                }
            }

            if (bestCandidate)
            {
                LOGI("[AutoFix] Rescanned NamePrivate=0x%lx (was 0x%lx) anchor='%s' @ idx=%d",
                     (unsigned long)bestCandidate,
                     (unsigned long)before,
                     bestAnchor.c_str(),
                     bestIndex);
                off->UObject.NamePrivate = bestCandidate;
                return true;
            }

            const uintptr_t dumpObject = reinterpret_cast<uintptr_t>(Objects()->GetObjectPtr(0));
            if (IsLikelyPtr(dumpObject))
                DumpObjectNameCandidates(0, dumpObject);

            LOGE("[AutoFix] UObject.NamePrivate fixup failed; keeping preset");
            return false;
        }

        bool FollowSuperChain(uintptr_t start, uintptr_t superOffset, uintptr_t nameOffset, std::string *endName, int *depthOut)
        {
            if (!start || !superOffset || !nameOffset)
                return false;

            uintptr_t current = start;
            int depth = 0;
            std::string lastName;

            while (current && depth < 32)
            {
                current = ReadPtr(current + superOffset);
                if (!IsLikelyPtr(current))
                    break;

                lastName = ReadObjectName(current, nameOffset);
                if (lastName == "Object")
                {
                    if (endName)
                        *endName = lastName;
                    if (depthOut)
                        *depthOut = depth + 1;
                    return true;
                }
                ++depth;
            }

            if (endName)
                *endName = lastName;
            if (depthOut)
                *depthOut = depth;
            return false;
        }

        bool FixupSuperStruct()
        {
            UE_Offsets *off = Offsets();
            if (!off || off->UObject.NamePrivate == 0 || !Objects())
                return false;

            const uintptr_t before = off->UStruct.SuperStruct;
            const UE_UClass worldClass = Objects()->FindObject("Class Engine.World").Cast<UE_UClass>();
            if (!worldClass)
            {
                LOGW("[AutoFix] UStruct anchor 'World' class not found");
                return false;
            }

            const uintptr_t worldAddr = reinterpret_cast<uintptr_t>(worldClass.GetAddress());
            std::string endName;
            int depth = 0;

            if (before && FollowSuperChain(worldAddr, before, off->UObject.NamePrivate, &endName, &depth))
            {
                LOGI("[AutoFix] UStruct.SuperStruct=0x%lx validated (chain reached '%s' depth=%d)",
                     (unsigned long)before,
                     endName.c_str(),
                     depth);
                return true;
            }

            if (before)
            {
                LOGW("[AutoFix] UStruct.SuperStruct=0x%lx: chain didn't reach Object", (unsigned long)before);
            }

            for (uintptr_t candidate = 0x0; candidate <= 0x100; candidate += 0x4)
            {
                if (!FollowSuperChain(worldAddr, candidate, off->UObject.NamePrivate, &endName, &depth))
                    continue;

                LOGI("[AutoFix] Rescanned UStruct.SuperStruct=0x%lx (was 0x%lx) chain end='%s'",
                     (unsigned long)candidate,
                     (unsigned long)before,
                     endName.c_str());
                off->UStruct.SuperStruct = candidate;
                return true;
            }

            LOGW("[AutoFix] UStruct.SuperStruct fixup failed; keeping preset");
            return false;
        }

        bool FixupUFunctionFunc()
        {
            UE_Offsets *off = Offsets();
            if (!off || off->UObject.NamePrivate == 0 || !Objects())
                return false;

            const uintptr_t before = off->UFunction.Func;
            const UE_UClass functionClass = Objects()->FindObject("Class CoreUObject.Function").Cast<UE_UClass>();
            if (!functionClass)
            {
                LOGW("[AutoFix] UFunction.Func fixup failed; keeping preset");
                return false;
            }

            auto validateOffset = [&](uintptr_t candidate, bool logOk) -> bool
            {
                bool ok = false;
                Objects()->ForEachObjectOfClass(functionClass, [&](UE_UObject object)
                {
                    const uintptr_t address = reinterpret_cast<uintptr_t>(object.GetAddress());
                    const uintptr_t func = ReadPtr(address + candidate);
                    if (!IsExecutableInModule(func))
                        return false;

                    if (logOk)
                    {
                        LOGI("[AutoFix] UFunction.Func=0x%lx validated ('%s'.Func=%p)",
                             (unsigned long)candidate,
                             object.GetName().c_str(),
                             (void *)func);
                    }
                    ok = true;
                    return true;
                });
                return ok;
            };

            if (before && validateOffset(before, true))
                return true;

            int probed = 0;
            for (uintptr_t candidate = 0x80; candidate <= 0x140; candidate += 0x8)
            {
                ++probed;
                if (!validateOffset(candidate, false))
                    continue;

                std::string ownerName;
                Objects()->ForEachObjectOfClass(functionClass, [&](UE_UObject object)
                {
                    const uintptr_t address = reinterpret_cast<uintptr_t>(object.GetAddress());
                    const uintptr_t func = ReadPtr(address + candidate);
                    if (!IsExecutableInModule(func))
                        return false;
                    ownerName = object.GetName();
                    return true;
                });

                LOGI("[AutoFix] Rescanned UFunction.Func=0x%lx (was 0x%lx) on '%s'",
                     (unsigned long)candidate,
                     (unsigned long)before,
                     ownerName.c_str());
                off->UFunction.Func = candidate;
                return true;
            }

            LOGW("[AutoFix] UFunction.Func=0x%lx: no probed function had executable Func (probed=%d)",
                 (unsigned long)before,
                 probed);
            LOGW("[AutoFix] UFunction.Func fixup failed; keeping preset");
            return false;
        }
    }  // namespace

    bool RunFixup(IGameProfile *profile)
    {
        (void)profile;
        if (!Offsets() || !Objects() || !Vars())
        {
            LOGE("[AutoFix] RunFixup: UEWrappers not initialized");
            return false;
        }

        LOGI("[AutoFix] === RunFixup begin ===");
        ApplyDefaultsOnly();

        UE_Offsets *off = Offsets();
        UE_Offsets before = *off;

        const bool nameOk = FixupNamePrivate();
        const bool superOk = nameOk ? FixupSuperStruct() : false;
        const bool funcOk = nameOk ? FixupUFunctionFunc() : false;

        DumpAutoFoundOffsets(before, *off);

        const bool success = nameOk && superOk && funcOk;
        LOGI("[AutoFix] === RunFixup done (success=%d) ===", success ? 1 : 0);
        return success;
    }
}  // namespace AutoFix
