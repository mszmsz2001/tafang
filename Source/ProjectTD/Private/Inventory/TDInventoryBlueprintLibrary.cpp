#include "Inventory/TDInventoryBlueprintLibrary.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Inventory/TDInventoryActionHandler.h"
#include "UObject/Field.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace TDInventory
{
    const FName SlotsPropertyName(TEXT("Slots"));
    const FName MaxSlotsPropertyName(TEXT("MaxSlots"));
    const FName OnInventoryUpdatedPropertyName(TEXT("OnInventoryUpdated"));
    const FName EquipSlotsPropertyName(TEXT("EquipSlots"));
    const FName OnEquipmentUpdatePropertyName(TEXT("OnEquipmentUpdate"));
    const FName ItemIDPropertyName(TEXT("ItemID"));
    const FName ItemRowHandlePropertyName(TEXT("ItemRowHandle"));
    const FName QuantityPropertyName(TEXT("Quantity"));
    const FName CurrentDurabilityPropertyName(TEXT("CurrentDurability"));
    const FName IsStackablePropertyName(TEXT("bIsStackable"));
    const FName MaxStackSizePropertyName(TEXT("MaxStackSize"));
    const FName ItemTypePropertyName(TEXT("ItemType"));
    const FName EquipSlotPropertyName(TEXT("EquipSlot"));
    const FName DropMeshPropertyName(TEXT("DropMesh"));
    const TCHAR* DefaultItemTablePath = TEXT("/Game/ProjectTD/\u7ed3\u6784\u4e0e\u679a\u4e3e/DT_Item.DT_Item");

    struct FInventoryComponentAccess
    {
        UObject* Object = nullptr;
        FArrayProperty* SlotsProperty = nullptr;
        FIntProperty* MaxSlotsProperty = nullptr;
        FMulticastDelegateProperty* UpdatedDelegateProperty = nullptr;
    };

    struct FSlotStructAccess
    {
        UScriptStruct* Struct = nullptr;
        FNameProperty* ItemIDProperty = nullptr;
        FIntProperty* QuantityProperty = nullptr;
        FIntProperty* CurrentDurabilityProperty = nullptr;
    };

    struct FEquipmentComponentAccess
    {
        UObject* Object = nullptr;
        FMapProperty* EquipSlotsProperty = nullptr;
        FMulticastDelegateProperty* UpdatedDelegateProperty = nullptr;
    };

    struct FItemDataView
    {
        bool bFound = false;
        bool bIsStackable = false;
        int32 MaxStackSize = 1;
        int64 ItemTypeValue = 0;
        FString ItemTypeName;
        int64 EquipSlotValue = INDEX_NONE;
        FString EquipSlotName;
        UStaticMesh* DropMesh = nullptr;
    };

    AActor* ResolveActor(UObject* Context)
    {
        if (!Context)
        {
            return nullptr;
        }

        if (AActor* Actor = Cast<AActor>(Context))
        {
            return Actor;
        }

        if (UActorComponent* Component = Cast<UActorComponent>(Context))
        {
            return Component->GetOwner();
        }

        return nullptr;
    }

    UObject* FindCompatibleComponent(UObject* Context, const FString& Token)
    {
        if (!Context)
        {
            return nullptr;
        }

        const bool bContextIsInventoryLike = Token.Equals(TEXT("Inventory")) && FindFProperty<FArrayProperty>(Context->GetClass(), SlotsPropertyName);
        const bool bContextIsEquipmentLike = Token.Equals(TEXT("Equipment")) && FindFProperty<FMapProperty>(Context->GetClass(), EquipSlotsPropertyName);
        if (bContextIsInventoryLike || bContextIsEquipmentLike)
        {
            return Context;
        }

        for (TFieldIterator<FObjectProperty> It(Context->GetClass()); It; ++It)
        {
            FObjectProperty* ObjectProperty = *It;
            UObject* ReferencedObject = ObjectProperty->GetObjectPropertyValue_InContainer(Context);
            if (IsValid(ReferencedObject) && ReferencedObject->GetClass()->GetName().Contains(Token))
            {
                return ReferencedObject;
            }
        }

        AActor* Owner = ResolveActor(Context);
        if (!Owner)
        {
            return nullptr;
        }

        TInlineComponentArray<UActorComponent*> Components(Owner);
        for (UActorComponent* Component : Components)
        {
            if (IsValid(Component) && Component->GetClass()->GetName().Contains(Token))
            {
                return Component;
            }
        }

        return nullptr;
    }

    UDataTable* ResolveItemTable(UObject* InventoryObject)
    {
        if (InventoryObject)
        {
            for (TFieldIterator<FObjectProperty> It(InventoryObject->GetClass()); It; ++It)
            {
                FObjectProperty* ObjectProperty = *It;
                if (!ObjectProperty->PropertyClass->IsChildOf(UDataTable::StaticClass()))
                {
                    continue;
                }

                if (UDataTable* DataTable = Cast<UDataTable>(ObjectProperty->GetObjectPropertyValue_InContainer(InventoryObject)))
                {
                    return DataTable;
                }
            }
        }

        return LoadObject<UDataTable>(nullptr, DefaultItemTablePath);
    }

    bool ReadIntValue(const FProperty* Property, const void* ValuePtr, int32& OutValue)
    {
        if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            OutValue = IntProperty->GetPropertyValue(ValuePtr);
            return true;
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            OutValue = static_cast<int32>(ByteProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FUInt32Property* UInt32Property = CastField<FUInt32Property>(Property))
        {
            OutValue = static_cast<int32>(UInt32Property->GetPropertyValue(ValuePtr));
            return true;
        }

        return false;
    }

    bool ReadBoolValue(const FProperty* Property, const void* ValuePtr, bool& OutValue)
    {
        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            OutValue = BoolProperty->GetPropertyValue(ValuePtr);
            return true;
        }

        return false;
    }

    bool ReadNameValue(const FProperty* Property, const void* ValuePtr, FName& OutValue)
    {
        if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            OutValue = NameProperty->GetPropertyValue(ValuePtr);
            return true;
        }

        return false;
    }

    bool ReadObjectValue(const FProperty* Property, const void* ValuePtr, UObject*& OutValue)
    {
        if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
        {
            OutValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
            return true;
        }

        if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
        {
            OutValue = SoftObjectProperty->GetPropertyValue(ValuePtr).LoadSynchronous();
            return true;
        }

        return false;
    }

    bool ReadEnumDisplayValue(const FProperty* Property, const void* ValuePtr, int64& OutValue, FString& OutDisplay)
    {
        UEnum* Enum = nullptr;
        int64 NumericValue = 0;

        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            Enum = EnumProperty->GetEnum();
            NumericValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
        }
        else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            Enum = ByteProperty->Enum;
            NumericValue = ByteProperty->GetPropertyValue(ValuePtr);
        }
        else
        {
            return false;
        }

        OutValue = NumericValue;
        OutDisplay = Enum ? Enum->GetDisplayNameTextByValue(NumericValue).ToString() : FString();
        return true;
    }

    bool PropertyNameMatches(const FProperty* Property, const FName& ExactName, const TCHAR* FallbackToken)
    {
        if (!Property)
        {
            return false;
        }

        return Property->GetFName() == ExactName || Property->GetName().Contains(FallbackToken, ESearchCase::IgnoreCase);
    }

    FInventoryComponentAccess ResolveInventory(UObject* InventoryContext)
    {
        FInventoryComponentAccess Access;
        Access.Object = FindCompatibleComponent(InventoryContext, TEXT("Inventory"));
        if (!Access.Object)
        {
            return Access;
        }

        Access.SlotsProperty = FindFProperty<FArrayProperty>(Access.Object->GetClass(), SlotsPropertyName);
        Access.MaxSlotsProperty = FindFProperty<FIntProperty>(Access.Object->GetClass(), MaxSlotsPropertyName);
        Access.UpdatedDelegateProperty = FindFProperty<FMulticastDelegateProperty>(Access.Object->GetClass(), OnInventoryUpdatedPropertyName);

        if (!Access.SlotsProperty)
        {
            for (TFieldIterator<FArrayProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FArrayProperty* Candidate = *It;
                const FStructProperty* StructInner = CastField<FStructProperty>(Candidate->Inner);
                if (!StructInner || !StructInner->Struct)
                {
                    continue;
                }

                const bool bLooksLikeInventorySlot =
                    FindFProperty<FNameProperty>(StructInner->Struct, ItemIDPropertyName) &&
                    FindFProperty<FIntProperty>(StructInner->Struct, QuantityPropertyName);

                if (bLooksLikeInventorySlot)
                {
                    Access.SlotsProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.MaxSlotsProperty)
        {
            for (TFieldIterator<FIntProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FIntProperty* Candidate = *It;
                const FString CandidateName = Candidate->GetName();
                if (CandidateName.Contains(TEXT("Max"), ESearchCase::IgnoreCase) && CandidateName.Contains(TEXT("Slot"), ESearchCase::IgnoreCase))
                {
                    Access.MaxSlotsProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.UpdatedDelegateProperty)
        {
            for (TFieldIterator<FMulticastDelegateProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FMulticastDelegateProperty* Candidate = *It;
                const FString CandidateName = Candidate->GetName();
                if (CandidateName.Contains(TEXT("Inventory"), ESearchCase::IgnoreCase) &&
                    CandidateName.Contains(TEXT("Update"), ESearchCase::IgnoreCase))
                {
                    Access.UpdatedDelegateProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.SlotsProperty || !CastField<FStructProperty>(Access.SlotsProperty->Inner))
        {
            Access = FInventoryComponentAccess();
        }

        return Access;
    }

    FSlotStructAccess ResolveSlotAccess(const FArrayProperty* SlotsProperty)
    {
        FSlotStructAccess Access;
        if (!SlotsProperty)
        {
            return Access;
        }

        const FStructProperty* StructProperty = CastField<FStructProperty>(SlotsProperty->Inner);
        if (!StructProperty)
        {
            return Access;
        }

        Access.Struct = StructProperty->Struct;
        Access.ItemIDProperty = FindFProperty<FNameProperty>(Access.Struct, ItemIDPropertyName);
        Access.QuantityProperty = FindFProperty<FIntProperty>(Access.Struct, QuantityPropertyName);
        Access.CurrentDurabilityProperty = FindFProperty<FIntProperty>(Access.Struct, CurrentDurabilityPropertyName);

        if (!Access.ItemIDProperty)
        {
            for (TFieldIterator<FNameProperty> It(Access.Struct); It; ++It)
            {
                FNameProperty* Candidate = *It;
                if (Candidate->GetName().Contains(TEXT("ItemID"), ESearchCase::IgnoreCase))
                {
                    Access.ItemIDProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.QuantityProperty)
        {
            for (TFieldIterator<FIntProperty> It(Access.Struct); It; ++It)
            {
                FIntProperty* Candidate = *It;
                if (Candidate->GetName().Contains(TEXT("Quantity"), ESearchCase::IgnoreCase))
                {
                    Access.QuantityProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.CurrentDurabilityProperty)
        {
            for (TFieldIterator<FIntProperty> It(Access.Struct); It; ++It)
            {
                FIntProperty* Candidate = *It;
                if (Candidate->GetName().Contains(TEXT("Durability"), ESearchCase::IgnoreCase))
                {
                    Access.CurrentDurabilityProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.ItemIDProperty || !Access.QuantityProperty)
        {
            Access = FSlotStructAccess();
        }

        return Access;
    }

    FEquipmentComponentAccess ResolveEquipment(UObject* InventoryContext)
    {
        FEquipmentComponentAccess Access;
        Access.Object = FindCompatibleComponent(InventoryContext, TEXT("Equipment"));
        if (!Access.Object)
        {
            return Access;
        }

        Access.EquipSlotsProperty = FindFProperty<FMapProperty>(Access.Object->GetClass(), EquipSlotsPropertyName);
        Access.UpdatedDelegateProperty = FindFProperty<FMulticastDelegateProperty>(Access.Object->GetClass(), OnEquipmentUpdatePropertyName);
        if (!Access.EquipSlotsProperty)
        {
            Access = FEquipmentComponentAccess();
        }

        return Access;
    }

    void BroadcastDelegate(UObject* TargetObject, FMulticastDelegateProperty* DelegateProperty)
    {
        if (!TargetObject || !DelegateProperty)
        {
            return;
        }

        FMulticastScriptDelegate* ScriptDelegate = DelegateProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(TargetObject);
        if (ScriptDelegate)
        {
            ScriptDelegate->ProcessMulticastDelegate<UObject>(nullptr);
        }
    }

    FTDInventorySlotData ReadSlotData(const FSlotStructAccess& SlotAccess, const void* SlotPtr)
    {
        FTDInventorySlotData Data;
        if (!SlotAccess.Struct || !SlotPtr)
        {
            return Data;
        }

        Data.ItemID = SlotAccess.ItemIDProperty->GetPropertyValue_InContainer(SlotPtr);
        Data.Quantity = SlotAccess.QuantityProperty->GetPropertyValue_InContainer(SlotPtr);
        Data.CurrentDurability = SlotAccess.CurrentDurabilityProperty
            ? SlotAccess.CurrentDurabilityProperty->GetPropertyValue_InContainer(SlotPtr)
            : 0;
        Data.bIsEmpty = Data.ItemID.IsNone() || Data.Quantity <= 0;
        return Data;
    }

    void WriteSlotData(const FSlotStructAccess& SlotAccess, void* SlotPtr, const FTDInventorySlotData& Data)
    {
        if (!SlotAccess.Struct || !SlotPtr)
        {
            return;
        }

        SlotAccess.ItemIDProperty->SetPropertyValue_InContainer(SlotPtr, Data.bIsEmpty ? NAME_None : Data.ItemID);
        SlotAccess.QuantityProperty->SetPropertyValue_InContainer(SlotPtr, Data.bIsEmpty ? 0 : Data.Quantity);
        if (SlotAccess.CurrentDurabilityProperty)
        {
            SlotAccess.CurrentDurabilityProperty->SetPropertyValue_InContainer(SlotPtr, Data.bIsEmpty ? 0 : Data.CurrentDurability);
        }
    }

    int32 GetDesiredSlotCount(const FInventoryComponentAccess& Access)
    {
        if (Access.MaxSlotsProperty && Access.Object)
        {
            return FMath::Max(0, Access.MaxSlotsProperty->GetPropertyValue_InContainer(Access.Object));
        }

        return 0;
    }

    void EnsureSlotCapacity(const FInventoryComponentAccess& Access, FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess)
    {
        const int32 DesiredSlots = GetDesiredSlotCount(Access);
        if (DesiredSlots <= 0 || SlotsHelper.Num() >= DesiredSlots)
        {
            return;
        }

        const int32 PreviousNum = SlotsHelper.Num();
        SlotsHelper.Resize(DesiredSlots);
        for (int32 Index = PreviousNum; Index < DesiredSlots; ++Index)
        {
            WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index), FTDInventorySlotData());
        }
    }

    int32 FindFirstEmptySlot(FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess)
    {
        for (int32 Index = 0; Index < SlotsHelper.Num(); ++Index)
        {
            if (ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index)).bIsEmpty)
            {
                return Index;
            }
        }

        return INDEX_NONE;
    }

    bool IsVirtualOrEmptySlot(FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess, int32 SlotIndex)
    {
        if (SlotIndex < 0)
        {
            return false;
        }

        if (!SlotsHelper.IsValidIndex(SlotIndex))
        {
            return true;
        }

        return ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex)).bIsEmpty;
    }

    int32 CountEmptySlots(FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess, int32 DesiredSlotCount)
    {
        int32 EmptySlotCount = 0;
        for (int32 Index = 0; Index < DesiredSlotCount; ++Index)
        {
            if (IsVirtualOrEmptySlot(SlotsHelper, SlotAccess, Index))
            {
                ++EmptySlotCount;
            }
        }

        return EmptySlotCount;
    }

    FItemDataView GetItemData(UObject* InventoryObject, FName ItemID)
    {
        FItemDataView Data;
        if (ItemID.IsNone())
        {
            return Data;
        }

        UDataTable* ItemTable = ResolveItemTable(InventoryObject);
        if (!ItemTable)
        {
            return Data;
        }

        const uint8* RowData = ItemTable->FindRowUnchecked(ItemID);
        if (!RowData || !ItemTable->RowStruct)
        {
            return Data;
        }

        Data.bFound = true;
        for (TFieldIterator<FProperty> It(ItemTable->RowStruct); It; ++It)
        {
            FProperty* Property = *It;
            const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);

            if (PropertyNameMatches(Property, IsStackablePropertyName, TEXT("Stackable")))
            {
                ReadBoolValue(Property, ValuePtr, Data.bIsStackable);
            }
            else if (PropertyNameMatches(Property, MaxStackSizePropertyName, TEXT("MaxStack")))
            {
                ReadIntValue(Property, ValuePtr, Data.MaxStackSize);
            }
            else if (PropertyNameMatches(Property, ItemTypePropertyName, TEXT("ItemType")))
            {
                ReadEnumDisplayValue(Property, ValuePtr, Data.ItemTypeValue, Data.ItemTypeName);
            }
            else if (PropertyNameMatches(Property, EquipSlotPropertyName, TEXT("EquipSlot")))
            {
                ReadEnumDisplayValue(Property, ValuePtr, Data.EquipSlotValue, Data.EquipSlotName);
            }
            else if (PropertyNameMatches(Property, DropMeshPropertyName, TEXT("DropMesh")))
            {
                UObject* LoadedObject = nullptr;
                if (ReadObjectValue(Property, ValuePtr, LoadedObject))
                {
                    Data.DropMesh = Cast<UStaticMesh>(LoadedObject);
                }
            }
        }

        Data.MaxStackSize = FMath::Max(Data.MaxStackSize, 1);
        return Data;
    }

    bool IsStackableItem(const FItemDataView& ItemData)
    {
        return ItemData.bFound && ItemData.bIsStackable && ItemData.MaxStackSize > 1;
    }

    bool IsSameItem(const FTDInventorySlotData& A, const FTDInventorySlotData& B)
    {
        return !A.bIsEmpty && !B.bIsEmpty && A.ItemID == B.ItemID;
    }

    bool RemoveQuantityAtSlot(FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess, int32 SlotIndex, int32 QuantityToRemove, int32& QuantityRemoved)
    {
        QuantityRemoved = 0;
        if (!SlotsHelper.IsValidIndex(SlotIndex) || QuantityToRemove <= 0)
        {
            return false;
        }

        FTDInventorySlotData SlotData = ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex));
        if (SlotData.bIsEmpty)
        {
            return false;
        }

        QuantityRemoved = FMath::Min(QuantityToRemove, SlotData.Quantity);
        SlotData.Quantity -= QuantityRemoved;
        if (SlotData.Quantity <= 0)
        {
            SlotData = FTDInventorySlotData();
        }

        WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex), SlotData);
        return QuantityRemoved > 0;
    }

    int32 CountItemInSlots(FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess, FName ItemID)
    {
        if (ItemID.IsNone())
        {
            return 0;
        }

        int32 TotalQuantity = 0;
        for (int32 Index = 0; Index < SlotsHelper.Num(); ++Index)
        {
            const FTDInventorySlotData SlotData = ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
            if (!SlotData.bIsEmpty && SlotData.ItemID == ItemID && SlotData.Quantity > 0)
            {
                TotalQuantity += SlotData.Quantity;
            }
        }

        return TotalQuantity;
    }

    bool BuildRequirementMap(const TArray<FTDInventoryItemRequirement>& Requirements, TMap<FName, int32>& OutRequirements)
    {
        OutRequirements.Reset();
        for (const FTDInventoryItemRequirement& Requirement : Requirements)
        {
            if (Requirement.Quantity <= 0)
            {
                continue;
            }

            if (Requirement.ItemID.IsNone())
            {
                UE_LOG(LogTemp, Warning, TEXT("Inventory requirement has empty ItemID."));
                return false;
            }

            int32& RequiredQuantity = OutRequirements.FindOrAdd(Requirement.ItemID);
            RequiredQuantity += Requirement.Quantity;
        }

        return true;
    }

    bool ReadRequirementItemID(const FStructProperty* RequirementStructProperty, const void* RequirementPtr, FName& OutItemID)
    {
        OutItemID = NAME_None;
        if (!RequirementStructProperty || !RequirementStructProperty->Struct || !RequirementPtr)
        {
            return false;
        }

        if (const FNameProperty* ItemIDProperty = FindFProperty<FNameProperty>(RequirementStructProperty->Struct, ItemIDPropertyName))
        {
            OutItemID = ItemIDProperty->GetPropertyValue_InContainer(RequirementPtr);
            return !OutItemID.IsNone();
        }

        for (TFieldIterator<FNameProperty> It(RequirementStructProperty->Struct); It; ++It)
        {
            FNameProperty* Candidate = *It;
            if (PropertyNameMatches(Candidate, ItemIDPropertyName, TEXT("ItemID")))
            {
                OutItemID = Candidate->GetPropertyValue_InContainer(RequirementPtr);
                return !OutItemID.IsNone();
            }
        }

        for (TFieldIterator<FStructProperty> It(RequirementStructProperty->Struct); It; ++It)
        {
            FStructProperty* Candidate = *It;
            if (!PropertyNameMatches(Candidate, ItemRowHandlePropertyName, TEXT("ItemRowHandle")) ||
                Candidate->Struct != FDataTableRowHandle::StaticStruct())
            {
                continue;
            }

            const FDataTableRowHandle* RowHandle = Candidate->ContainerPtrToValuePtr<FDataTableRowHandle>(RequirementPtr);
            if (RowHandle)
            {
                OutItemID = RowHandle->RowName;
                return !OutItemID.IsNone();
            }
        }

        return false;
    }

    bool ReadRequirementQuantity(const FStructProperty* RequirementStructProperty, const void* RequirementPtr, int32& OutQuantity)
    {
        OutQuantity = 0;
        if (!RequirementStructProperty || !RequirementStructProperty->Struct || !RequirementPtr)
        {
            return false;
        }

        if (const FIntProperty* QuantityProperty = FindFProperty<FIntProperty>(RequirementStructProperty->Struct, QuantityPropertyName))
        {
            OutQuantity = QuantityProperty->GetPropertyValue_InContainer(RequirementPtr);
            return true;
        }

        for (TFieldIterator<FProperty> It(RequirementStructProperty->Struct); It; ++It)
        {
            FProperty* Candidate = *It;
            if (PropertyNameMatches(Candidate, QuantityPropertyName, TEXT("Quantity")))
            {
                return ReadIntValue(Candidate, Candidate->ContainerPtrToValuePtr<void>(RequirementPtr), OutQuantity);
            }
        }

        return false;
    }

    bool BuildRequirementMapFromArray(const void* RequirementsArray, const FArrayProperty* RequirementsArrayProperty, TMap<FName, int32>& OutRequirements)
    {
        OutRequirements.Reset();
        if (!RequirementsArray || !RequirementsArrayProperty)
        {
            return false;
        }

        const FStructProperty* RequirementStructProperty = CastField<FStructProperty>(RequirementsArrayProperty->Inner);
        if (!RequirementStructProperty)
        {
            UE_LOG(LogTemp, Warning, TEXT("Inventory requirements must be a struct array."));
            return false;
        }

        FScriptArrayHelper RequirementsHelper(RequirementsArrayProperty, RequirementsArray);
        for (int32 Index = 0; Index < RequirementsHelper.Num(); ++Index)
        {
            const void* RequirementPtr = RequirementsHelper.GetRawPtr(Index);

            int32 Quantity = 0;
            if (!ReadRequirementQuantity(RequirementStructProperty, RequirementPtr, Quantity))
            {
                UE_LOG(LogTemp, Warning, TEXT("Inventory requirement is missing Quantity."));
                return false;
            }

            if (Quantity <= 0)
            {
                continue;
            }

            FName ItemID = NAME_None;
            if (!ReadRequirementItemID(RequirementStructProperty, RequirementPtr, ItemID))
            {
                UE_LOG(LogTemp, Warning, TEXT("Inventory requirement is missing ItemID or ItemRowHandle."));
                return false;
            }

            int32& RequiredQuantity = OutRequirements.FindOrAdd(ItemID);
            RequiredQuantity += Quantity;
        }

        return true;
    }

    bool HasEnoughItemsFromMap(UActorComponent* InventoryComponent, const TMap<FName, int32>& RequiredItems)
    {
        if (RequiredItems.IsEmpty())
        {
            return true;
        }

        if (!InventoryComponent)
        {
            return false;
        }

        const FInventoryComponentAccess Access = ResolveInventory(InventoryComponent);
        const FSlotStructAccess SlotAccess = ResolveSlotAccess(Access.SlotsProperty);
        if (!Access.Object || !SlotAccess.Struct)
        {
            return false;
        }

        FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
        for (const TPair<FName, int32>& RequiredItem : RequiredItems)
        {
            const int32 AvailableQuantity = CountItemInSlots(SlotsHelper, SlotAccess, RequiredItem.Key);
            if (AvailableQuantity < RequiredItem.Value)
            {
                UE_LOG(
                    LogTemp,
                    Verbose,
                    TEXT("Inventory requirement not met: %s need %d, have %d."),
                    *RequiredItem.Key.ToString(),
                    RequiredItem.Value,
                    AvailableQuantity
                );
                return false;
            }
        }

        return true;
    }

    bool RemoveItemsFromMap(UActorComponent* InventoryComponent, const TMap<FName, int32>& RequiredItems)
    {
        if (RequiredItems.IsEmpty())
        {
            return true;
        }

        if (!InventoryComponent)
        {
            return false;
        }

        const FInventoryComponentAccess Access = ResolveInventory(InventoryComponent);
        const FSlotStructAccess SlotAccess = ResolveSlotAccess(Access.SlotsProperty);
        if (!Access.Object || !SlotAccess.Struct)
        {
            return false;
        }

        FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
        for (const TPair<FName, int32>& RequiredItem : RequiredItems)
        {
            const int32 AvailableQuantity = CountItemInSlots(SlotsHelper, SlotAccess, RequiredItem.Key);
            if (AvailableQuantity < RequiredItem.Value)
            {
                UE_LOG(
                    LogTemp,
                    Verbose,
                    TEXT("TryRemoveItems failed: %s need %d, have %d."),
                    *RequiredItem.Key.ToString(),
                    RequiredItem.Value,
                    AvailableQuantity
                );
                return false;
            }
        }

        for (const TPair<FName, int32>& RequiredItem : RequiredItems)
        {
            int32 RemainingToRemove = RequiredItem.Value;
            for (int32 Index = 0; Index < SlotsHelper.Num() && RemainingToRemove > 0; ++Index)
            {
                const FTDInventorySlotData SlotData = ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
                if (SlotData.bIsEmpty || SlotData.ItemID != RequiredItem.Key)
                {
                    continue;
                }

                int32 RemovedThisSlot = 0;
                RemoveQuantityAtSlot(SlotsHelper, SlotAccess, Index, RemainingToRemove, RemovedThisSlot);
                RemainingToRemove -= RemovedThisSlot;
            }
        }

        BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
        return true;
    }

    bool TryEquipFromInventory(UObject* InventoryContext, FScriptArrayHelper& SlotsHelper, const FSlotStructAccess& SlotAccess, int32 SlotIndex)
    {
        if (!SlotsHelper.IsValidIndex(SlotIndex))
        {
            return false;
        }

        FTDInventorySlotData InventorySlot = ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex));
        if (InventorySlot.bIsEmpty)
        {
            return false;
        }

        const FItemDataView ItemData = GetItemData(ResolveInventory(InventoryContext).Object, InventorySlot.ItemID);
        if (!ItemData.bFound || ItemData.EquipSlotValue == INDEX_NONE)
        {
            return false;
        }

        FEquipmentComponentAccess EquipmentAccess = ResolveEquipment(InventoryContext);
        if (!EquipmentAccess.Object || !EquipmentAccess.EquipSlotsProperty)
        {
            return false;
        }

        FScriptMapHelper MapHelper(EquipmentAccess.EquipSlotsProperty, EquipmentAccess.EquipSlotsProperty->ContainerPtrToValuePtr<void>(EquipmentAccess.Object));
        FProperty* KeyProperty = EquipmentAccess.EquipSlotsProperty->KeyProp;
        FProperty* ValueProperty = EquipmentAccess.EquipSlotsProperty->ValueProp;
        FStructProperty* ValueStructProperty = CastField<FStructProperty>(ValueProperty);
        if (!KeyProperty || !ValueStructProperty || ValueStructProperty->Struct != SlotAccess.Struct)
        {
            return false;
        }

        int32 ExistingMapIndex = INDEX_NONE;
        bool bWriteEquippedItemBackToSourceSlot = false;
        FTDInventorySlotData PreviouslyEquippedSlot;
        for (int32 MapIndex = 0; MapIndex < MapHelper.GetMaxIndex(); ++MapIndex)
        {
            if (!MapHelper.IsValidIndex(MapIndex))
            {
                continue;
            }

            void* KeyPtr = MapHelper.GetKeyPtr(MapIndex);
            int32 ExistingKey = INDEX_NONE;
            if (ReadIntValue(KeyProperty, KeyPtr, ExistingKey) && ExistingKey == ItemData.EquipSlotValue)
            {
                ExistingMapIndex = MapIndex;
                break;
            }
        }

        if (ExistingMapIndex != INDEX_NONE)
        {
            FTDInventorySlotData EquippedSlot = ReadSlotData(SlotAccess, MapHelper.GetValuePtr(ExistingMapIndex));
            if (!EquippedSlot.bIsEmpty)
            {
                if (InventorySlot.Quantity == 1)
                {
                    bWriteEquippedItemBackToSourceSlot = true;
                    PreviouslyEquippedSlot = EquippedSlot;
                }
                else
                {
                    int32 AddedBackQuantity = 0;
                    if (!UTDInventoryBlueprintLibrary::TryAddItem(InventoryContext, EquippedSlot.ItemID, EquippedSlot.Quantity, AddedBackQuantity) || AddedBackQuantity != EquippedSlot.Quantity)
                    {
                        return false;
                    }
                }
            }

            MapHelper.RemoveAt(ExistingMapIndex);
        }

        const int32 NewMapIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
        if (NewMapIndex == INDEX_NONE)
        {
            return false;
        }

        int32 EquipSlotValue = static_cast<int32>(ItemData.EquipSlotValue);
        if (FEnumProperty* EnumKeyProperty = CastField<FEnumProperty>(KeyProperty))
        {
            EnumKeyProperty->GetUnderlyingProperty()->SetIntPropertyValue(MapHelper.GetKeyPtr(NewMapIndex), static_cast<int64>(EquipSlotValue));
        }
        else if (FByteProperty* ByteKeyProperty = CastField<FByteProperty>(KeyProperty))
        {
            ByteKeyProperty->SetPropertyValue(MapHelper.GetKeyPtr(NewMapIndex), static_cast<uint8>(EquipSlotValue));
        }
        else
        {
            MapHelper.RemoveAt(NewMapIndex);
            return false;
        }

        FTDInventorySlotData EquippedCopy = InventorySlot;
        EquippedCopy.Quantity = 1;
        EquippedCopy.bIsEmpty = false;
        WriteSlotData(SlotAccess, MapHelper.GetValuePtr(NewMapIndex), EquippedCopy);
        MapHelper.Rehash();

        int32 RemovedQuantity = 0;
        if (!RemoveQuantityAtSlot(SlotsHelper, SlotAccess, SlotIndex, 1, RemovedQuantity))
        {
            MapHelper.RemoveAt(NewMapIndex);
            return false;
        }

        if (bWriteEquippedItemBackToSourceSlot)
        {
            PreviouslyEquippedSlot.bIsEmpty = false;
            WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex), PreviouslyEquippedSlot);
        }

        BroadcastDelegate(EquipmentAccess.Object, EquipmentAccess.UpdatedDelegateProperty);
        return true;
    }

    bool TryHandleUseAction(UObject* InventoryContext, int32 SlotIndex, const FTDInventorySlotData& SlotData, int32 Quantity)
    {
        UObject* HandlerObject = InventoryContext;
        if (AActor* Owner = ResolveActor(InventoryContext))
        {
            HandlerObject = Owner;
        }

        if (HandlerObject && HandlerObject->GetClass()->ImplementsInterface(UTDInventoryActionHandler::StaticClass()))
        {
            return ITDInventoryActionHandler::Execute_HandleInventoryUseItem(HandlerObject, SlotData.ItemID, SlotIndex, Quantity);
        }

        return true;
    }
}

UObject* UTDInventoryBlueprintLibrary::ResolveInventoryComponent(UObject* InventoryContext)
{
    return TDInventory::ResolveInventory(InventoryContext).Object;
}

FString UTDInventoryBlueprintLibrary::GetInventoryDebugSummary(UObject* InventoryContext)
{
    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    if (!InventoryContext)
    {
        return TEXT("InventoryContext=null");
    }

    FString Summary = FString::Printf(
        TEXT("Context=%s, Resolved=%s"),
        *InventoryContext->GetClass()->GetName(),
        Access.Object ? *Access.Object->GetClass()->GetName() : TEXT("null")
    );

    Summary += FString::Printf(
        TEXT(", SlotsProp=%s, MaxSlotsProp=%s, Delegate=%s"),
        Access.SlotsProperty ? *Access.SlotsProperty->GetName() : TEXT("null"),
        Access.MaxSlotsProperty ? *Access.MaxSlotsProperty->GetName() : TEXT("null"),
        Access.UpdatedDelegateProperty ? *Access.UpdatedDelegateProperty->GetName() : TEXT("null")
    );

    if (Access.Object && Access.SlotsProperty)
    {
        FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
        Summary += FString::Printf(TEXT(", RawSlots=%d"), SlotsHelper.Num());

        const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
        if (SlotAccess.Struct)
        {
            int32 NonEmptySlotCount = 0;
            FString NonEmptySlotSummary;
            for (int32 Index = 0; Index < SlotsHelper.Num(); ++Index)
            {
                const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
                if (SlotData.bIsEmpty)
                {
                    continue;
                }

                ++NonEmptySlotCount;
                const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, SlotData.ItemID);
                NonEmptySlotSummary += FString::Printf(
                    TEXT(" [%d:%s x%d Dur=%d Stackable=%s MaxStack=%d Data=%s]"),
                    Index,
                    *SlotData.ItemID.ToString(),
                    SlotData.Quantity,
                    SlotData.CurrentDurability,
                    ItemData.bIsStackable ? TEXT("true") : TEXT("false"),
                    ItemData.MaxStackSize,
                    ItemData.bFound ? TEXT("found") : TEXT("missing")
                );
            }

            Summary += FString::Printf(TEXT(", NonEmptySlots=%d"), NonEmptySlotCount);
            if (!NonEmptySlotSummary.IsEmpty())
            {
                Summary += TEXT(", Items=");
                Summary += NonEmptySlotSummary;
            }
        }
        else
        {
            Summary += TEXT(", SlotStruct=unresolved");
        }
    }

    if (Access.Object && Access.MaxSlotsProperty)
    {
        Summary += FString::Printf(TEXT(", MaxSlots=%d"), Access.MaxSlotsProperty->GetPropertyValue_InContainer(Access.Object));
    }

    return Summary;
}

int32 UTDInventoryBlueprintLibrary::GetInventoryMaxSlots(UObject* InventoryContext)
{
    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    return TDInventory::GetDesiredSlotCount(Access);
}

bool UTDInventoryBlueprintLibrary::TryAddItem(UObject* InventoryContext, FName ItemID, int32 Quantity, int32& QuantityAdded)
{
    QuantityAdded = 0;
    if (Quantity <= 0 || ItemID.IsNone())
    {
        return false;
    }

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, ItemID);
    const bool bIsStackable = TDInventory::IsStackableItem(ItemData);
    const int32 DesiredSlotCount = FMath::Max(SlotsHelper.Num(), TDInventory::GetDesiredSlotCount(Access));
    if (DesiredSlotCount <= 0)
    {
        return false;
    }

    int32 RemainingCapacityCheck = Quantity;
    if (bIsStackable)
    {
        for (int32 Index = 0; Index < SlotsHelper.Num() && RemainingCapacityCheck > 0; ++Index)
        {
            const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
            if (SlotData.bIsEmpty || SlotData.ItemID != ItemID)
            {
                continue;
            }

            RemainingCapacityCheck -= FMath::Max(0, ItemData.MaxStackSize - SlotData.Quantity);
        }
    }

    const int32 EmptySlotCount = TDInventory::CountEmptySlots(SlotsHelper, SlotAccess, DesiredSlotCount);
    const int32 EmptySlotCapacity = bIsStackable ? EmptySlotCount * ItemData.MaxStackSize : EmptySlotCount;
    RemainingCapacityCheck -= EmptySlotCapacity;
    if (RemainingCapacityCheck > 0)
    {
        return false;
    }

    TDInventory::EnsureSlotCapacity(Access, SlotsHelper, SlotAccess);
    int32 RemainingQuantity = Quantity;

    if (bIsStackable)
    {
        for (int32 Index = 0; Index < SlotsHelper.Num() && RemainingQuantity > 0; ++Index)
        {
            FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
            if (SlotData.bIsEmpty || SlotData.ItemID != ItemID)
            {
                continue;
            }

            const int32 AvailableSpace = ItemData.MaxStackSize - SlotData.Quantity;
            if (AvailableSpace <= 0)
            {
                continue;
            }

            const int32 QuantityToAdd = FMath::Min(AvailableSpace, RemainingQuantity);
            SlotData.Quantity += QuantityToAdd;
            SlotData.bIsEmpty = false;
            TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index), SlotData);
            RemainingQuantity -= QuantityToAdd;
            QuantityAdded += QuantityToAdd;
        }
    }

    while (RemainingQuantity > 0)
    {
        const int32 EmptySlotIndex = TDInventory::FindFirstEmptySlot(SlotsHelper, SlotAccess);
        if (EmptySlotIndex == INDEX_NONE)
        {
            break;
        }

        FTDInventorySlotData NewSlot;
        NewSlot.ItemID = ItemID;
        NewSlot.Quantity = bIsStackable ? FMath::Min(ItemData.MaxStackSize, RemainingQuantity) : 1;
        NewSlot.CurrentDurability = 0;
        NewSlot.bIsEmpty = false;
        TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(EmptySlotIndex), NewSlot);
        RemainingQuantity -= NewSlot.Quantity;
        QuantityAdded += NewSlot.Quantity;
    }

    const bool bChanged = QuantityAdded == Quantity;
    if (bChanged)
    {
        TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
    }

    return bChanged;
}

bool UTDInventoryBlueprintLibrary::TryRemoveItem(UObject* InventoryContext, FName ItemID, int32 Quantity, int32& QuantityRemoved)
{
    QuantityRemoved = 0;
    if (Quantity <= 0 || ItemID.IsNone())
    {
        return false;
    }

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));

    int32 AvailableQuantity = 0;
    for (int32 Index = 0; Index < SlotsHelper.Num(); ++Index)
    {
        const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
        if (!SlotData.bIsEmpty && SlotData.ItemID == ItemID)
        {
            AvailableQuantity += SlotData.Quantity;
            if (AvailableQuantity >= Quantity)
            {
                break;
            }
        }
    }

    if (AvailableQuantity < Quantity)
    {
        return false;
    }

    for (int32 Index = 0; Index < SlotsHelper.Num() && QuantityRemoved < Quantity; ++Index)
    {
        const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index));
        if (SlotData.bIsEmpty || SlotData.ItemID != ItemID)
        {
            continue;
        }

        const int32 RemainingToRemove = Quantity - QuantityRemoved;
        int32 RemovedThisSlot = 0;
        if (TDInventory::RemoveQuantityAtSlot(SlotsHelper, SlotAccess, Index, RemainingToRemove, RemovedThisSlot))
        {
            QuantityRemoved += RemovedThisSlot;
        }
    }

    const bool bChanged = QuantityRemoved == Quantity;
    if (bChanged)
    {
        TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
    }

    return bChanged;
}

int32 UTDInventoryBlueprintLibrary::GetItemCount(UActorComponent* InventoryComponent, FName ItemID)
{
    if (!InventoryComponent || ItemID.IsNone())
    {
        return 0;
    }

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryComponent);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return 0;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    return TDInventory::CountItemInSlots(SlotsHelper, SlotAccess, ItemID);
}

bool UTDInventoryBlueprintLibrary::HasEnoughItems(UActorComponent* InventoryComponent, const TArray<int32>& Requirements)
{
    checkNoEntry();
    return false;
}

bool UTDInventoryBlueprintLibrary::TryRemoveItems(UActorComponent* InventoryComponent, const TArray<int32>& Requirements)
{
    checkNoEntry();
    return false;
}

DEFINE_FUNCTION(UTDInventoryBlueprintLibrary::execHasEnoughItems)
{
    P_GET_OBJECT(UActorComponent, InventoryComponent);

    Stack.MostRecentProperty = nullptr;
    Stack.StepCompiledIn<FArrayProperty>(nullptr);
    const void* RequirementsArray = Stack.MostRecentPropertyAddress;
    const FArrayProperty* RequirementsArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);
    if (!RequirementsArrayProperty)
    {
        Stack.bArrayContextFailed = true;
        return;
    }

    P_FINISH;
    P_NATIVE_BEGIN;
    TMap<FName, int32> RequiredItems;
    *(bool*)RESULT_PARAM =
        TDInventory::BuildRequirementMapFromArray(RequirementsArray, RequirementsArrayProperty, RequiredItems) &&
        TDInventory::HasEnoughItemsFromMap(InventoryComponent, RequiredItems);
    P_NATIVE_END;
}

DEFINE_FUNCTION(UTDInventoryBlueprintLibrary::execTryRemoveItems)
{
    P_GET_OBJECT(UActorComponent, InventoryComponent);

    Stack.MostRecentProperty = nullptr;
    Stack.StepCompiledIn<FArrayProperty>(nullptr);
    const void* RequirementsArray = Stack.MostRecentPropertyAddress;
    const FArrayProperty* RequirementsArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);
    if (!RequirementsArrayProperty)
    {
        Stack.bArrayContextFailed = true;
        return;
    }

    P_FINISH;
    P_NATIVE_BEGIN;
    TMap<FName, int32> RequiredItems;
    *(bool*)RESULT_PARAM =
        TDInventory::BuildRequirementMapFromArray(RequirementsArray, RequirementsArrayProperty, RequiredItems) &&
        TDInventory::RemoveItemsFromMap(InventoryComponent, RequiredItems);
    P_NATIVE_END;
}

bool UTDInventoryBlueprintLibrary::MoveItem(UObject* InventoryContext, int32 FromIndex, int32 ToIndex)
{
    if (FromIndex == ToIndex)
    {
        return false;
    }

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    if (!SlotsHelper.IsValidIndex(FromIndex) || !SlotsHelper.IsValidIndex(ToIndex))
    {
        return false;
    }

    FTDInventorySlotData FromSlot = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(FromIndex));
    FTDInventorySlotData ToSlot = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(ToIndex));
    if (FromSlot.bIsEmpty)
    {
        return false;
    }

    bool bChanged = false;
    const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, FromSlot.ItemID);
    if (!ToSlot.bIsEmpty && TDInventory::IsSameItem(FromSlot, ToSlot) && TDInventory::IsStackableItem(ItemData))
    {
        const int32 AvailableSpace = ItemData.MaxStackSize - ToSlot.Quantity;
        const int32 QuantityToMove = FMath::Min(AvailableSpace, FromSlot.Quantity);
        if (QuantityToMove > 0)
        {
            ToSlot.Quantity += QuantityToMove;
            FromSlot.Quantity -= QuantityToMove;
            if (FromSlot.Quantity <= 0)
            {
                FromSlot = FTDInventorySlotData();
            }

            TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(FromIndex), FromSlot);
            TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(ToIndex), ToSlot);
            bChanged = true;
        }
    }
    else
    {
        TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(FromIndex), ToSlot);
        TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(ToIndex), FromSlot);
        bChanged = true;
    }

    if (bChanged)
    {
        TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
    }

    return bChanged;
}

bool UTDInventoryBlueprintLibrary::SplitStack(UObject* InventoryContext, int32 SourceIndex, int32 SplitQuantity, int32& OutNewSlotIndex)
{
    OutNewSlotIndex = INDEX_NONE;
    if (SplitQuantity <= 0)
    {
        return false;
    }

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    if (!SlotsHelper.IsValidIndex(SourceIndex))
    {
        return false;
    }

    FTDInventorySlotData SourceSlot = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SourceIndex));
    if (SourceSlot.bIsEmpty || SourceSlot.Quantity <= SplitQuantity)
    {
        return false;
    }

    const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, SourceSlot.ItemID);
    if (!TDInventory::IsStackableItem(ItemData))
    {
        return false;
    }

    TDInventory::EnsureSlotCapacity(Access, SlotsHelper, SlotAccess);
    OutNewSlotIndex = TDInventory::FindFirstEmptySlot(SlotsHelper, SlotAccess);
    if (OutNewSlotIndex == INDEX_NONE)
    {
        return false;
    }

    SourceSlot.Quantity -= SplitQuantity;
    FTDInventorySlotData NewSlot = SourceSlot;
    NewSlot.Quantity = SplitQuantity;
    NewSlot.bIsEmpty = false;

    TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(SourceIndex), SourceSlot);
    TDInventory::WriteSlotData(SlotAccess, SlotsHelper.GetRawPtr(OutNewSlotIndex), NewSlot);
    TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
    return true;
}

bool UTDInventoryBlueprintLibrary::DropItem(UObject* InventoryContext, int32 SlotIndex, int32 Quantity, FVector DropLocation, int32& QuantityDropped)
{
    QuantityDropped = 0;

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    if (!SlotsHelper.IsValidIndex(SlotIndex))
    {
        return false;
    }

    const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex));
    if (SlotData.bIsEmpty)
    {
        return false;
    }

    if (Quantity > SlotData.Quantity)
    {
        return false;
    }

    const int32 DesiredDropCount = Quantity > 0 ? Quantity : SlotData.Quantity;
    if (DesiredDropCount <= 0)
    {
        return false;
    }

    const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, SlotData.ItemID);
    if (AActor* OwnerActor = TDInventory::ResolveActor(InventoryContext))
    {
        UWorld* World = OwnerActor->GetWorld();
        if (World && ItemData.DropMesh)
        {
            const FVector SpawnLocation = DropLocation.IsNearlyZero() ? OwnerActor->GetActorLocation() + OwnerActor->GetActorForwardVector() * 120.0f : DropLocation;
            AStaticMeshActor* DroppedActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SpawnLocation, OwnerActor->GetActorRotation());
            if (DroppedActor)
            {
                DroppedActor->GetStaticMeshComponent()->SetStaticMesh(ItemData.DropMesh);
            }
        }
    }

    int32 RemovedQuantity = 0;
    if (!TDInventory::RemoveQuantityAtSlot(SlotsHelper, SlotAccess, SlotIndex, DesiredDropCount, RemovedQuantity))
    {
        return false;
    }

    QuantityDropped = RemovedQuantity;
    TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
    return QuantityDropped > 0;
}

TArray<FTDInventorySlotData> UTDInventoryBlueprintLibrary::GetInventorySlots(UObject* InventoryContext)
{
    TArray<FTDInventorySlotData> Result;

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return Result;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    const int32 DesiredSlotCount = FMath::Max(SlotsHelper.Num(), TDInventory::GetDesiredSlotCount(Access));
    Result.Reserve(DesiredSlotCount);
    for (int32 Index = 0; Index < DesiredSlotCount; ++Index)
    {
        if (SlotsHelper.IsValidIndex(Index))
        {
            Result.Add(TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(Index)));
        }
        else
        {
            Result.Add(FTDInventorySlotData());
        }
    }

    return Result;
}

TArray<FTDInventoryActionEntry> UTDInventoryBlueprintLibrary::GetAvailableActionsForSlot(UObject* InventoryContext, int32 SlotIndex)
{
    TArray<FTDInventoryActionEntry> Actions;

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return Actions;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    if (!SlotsHelper.IsValidIndex(SlotIndex))
    {
        return Actions;
    }

    const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex));
    if (SlotData.bIsEmpty)
    {
        return Actions;
    }

    const TDInventory::FItemDataView ItemData = TDInventory::GetItemData(Access.Object, SlotData.ItemID);

    FTDInventoryActionEntry DropAction;
    DropAction.Action = ETDInventoryItemAction::Drop;
    DropAction.Label = FText::FromString(TEXT("Drop"));
    Actions.Add(DropAction);

    if (ItemData.ItemTypeName.Equals(TEXT("Consumable"), ESearchCase::IgnoreCase))
    {
        FTDInventoryActionEntry UseAction;
        UseAction.Action = ETDInventoryItemAction::Use;
        UseAction.Label = FText::FromString(TEXT("Use"));
        Actions.Add(UseAction);
    }

    if (ItemData.ItemTypeName.Equals(TEXT("Equipment"), ESearchCase::IgnoreCase) || ItemData.EquipSlotValue != INDEX_NONE)
    {
        FTDInventoryActionEntry EquipAction;
        EquipAction.Action = ETDInventoryItemAction::Equip;
        EquipAction.Label = FText::FromString(TEXT("Equip"));
        Actions.Add(EquipAction);
    }

    if (SlotData.Quantity > 1 && TDInventory::IsStackableItem(ItemData))
    {
        FTDInventoryActionEntry SplitAction;
        SplitAction.Action = ETDInventoryItemAction::Split;
        SplitAction.Label = FText::FromString(TEXT("Split"));
        Actions.Add(SplitAction);
    }

    return Actions;
}

bool UTDInventoryBlueprintLibrary::ExecuteItemAction(
    UObject* InventoryContext,
    int32 SlotIndex,
    ETDInventoryItemAction Action,
    int32 QuantityOverride,
    FVector DropLocation,
    int32& QuantityProcessed
)
{
    QuantityProcessed = 0;

    const TDInventory::FInventoryComponentAccess Access = TDInventory::ResolveInventory(InventoryContext);
    const TDInventory::FSlotStructAccess SlotAccess = TDInventory::ResolveSlotAccess(Access.SlotsProperty);
    if (!Access.Object || !SlotAccess.Struct)
    {
        return false;
    }

    FScriptArrayHelper SlotsHelper(Access.SlotsProperty, Access.SlotsProperty->ContainerPtrToValuePtr<void>(Access.Object));
    if (!SlotsHelper.IsValidIndex(SlotIndex))
    {
        return false;
    }

    const FTDInventorySlotData SlotData = TDInventory::ReadSlotData(SlotAccess, SlotsHelper.GetRawPtr(SlotIndex));
    if (SlotData.bIsEmpty)
    {
        return false;
    }

    switch (Action)
    {
    case ETDInventoryItemAction::Drop:
        return DropItem(InventoryContext, SlotIndex, QuantityOverride, DropLocation, QuantityProcessed);

    case ETDInventoryItemAction::Split:
    {
        int32 NewSlotIndex = INDEX_NONE;
        const bool bSplit = SplitStack(InventoryContext, SlotIndex, QuantityOverride, NewSlotIndex);
        if (bSplit)
        {
            QuantityProcessed = QuantityOverride > 0 ? QuantityOverride : SlotData.Quantity / 2;
        }
        return bSplit;
    }

    case ETDInventoryItemAction::Equip:
    {
        const bool bEquipped = TDInventory::TryEquipFromInventory(InventoryContext, SlotsHelper, SlotAccess, SlotIndex);
        if (bEquipped)
        {
            QuantityProcessed = 1;
            TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
        }
        return bEquipped;
    }

    case ETDInventoryItemAction::Use:
    {
        const int32 QuantityToUse = QuantityOverride > 0 ? FMath::Min(QuantityOverride, SlotData.Quantity) : 1;
        if (QuantityToUse <= 0 || !TDInventory::TryHandleUseAction(InventoryContext, SlotIndex, SlotData, QuantityToUse))
        {
            return false;
        }

        int32 RemovedQuantity = 0;
        if (!TDInventory::RemoveQuantityAtSlot(SlotsHelper, SlotAccess, SlotIndex, QuantityToUse, RemovedQuantity))
        {
            return false;
        }

        QuantityProcessed = RemovedQuantity;
        TDInventory::BroadcastDelegate(Access.Object, Access.UpdatedDelegateProperty);
        return QuantityProcessed > 0;
    }

    default:
        break;
    }

    return false;
}
