#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "TDInventoryBlueprintLibrary.generated.h"

UENUM(BlueprintType)
enum class ETDInventoryItemAction : uint8
{
    None,
    Drop,
    Use,
    Equip,
    Split
};

USTRUCT(BlueprintType)
struct PROJECTTD_API FTDInventorySlotData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    FName ItemID = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    int32 Quantity = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    int32 CurrentDurability = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    bool bIsEmpty = true;
};

USTRUCT(BlueprintType)
struct PROJECTTD_API FTDInventoryActionEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    ETDInventoryItemAction Action = ETDInventoryItemAction::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    FText Label;
};

USTRUCT(BlueprintType)
struct PROJECTTD_API FTDInventoryItemRequirement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    FName ItemID = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    int32 Quantity = 1;
};

UCLASS()
class PROJECTTD_API UTDInventoryBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static UObject* ResolveInventoryComponent(UObject* InventoryContext);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static FString GetInventoryDebugSummary(UObject* InventoryContext);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static FString GetEquipmentDebugSummary(UObject* EquipmentContext);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static int32 GetInventoryMaxSlots(UObject* InventoryContext);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory", meta = (CPP_Default_CurrentDurability = "-1.0"))
    static bool TryAddItem(UObject* InventoryContext, FName ItemID, int32 Quantity, float CurrentDurability, int32& QuantityAdded);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool TryRemoveItem(UObject* InventoryContext, FName ItemID, int32 Quantity, int32& QuantityRemoved);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static int32 GetItemCount(UActorComponent* InventoryComponent, FName ItemID);

    UFUNCTION(BlueprintCallable, CustomThunk, Category = "ProjectTD|Inventory", meta = (ArrayParm = "Requirements"))
    static bool HasEnoughItems(UActorComponent* InventoryComponent, const TArray<int32>& Requirements);
    DECLARE_FUNCTION(execHasEnoughItems);

    UFUNCTION(BlueprintCallable, CustomThunk, Category = "ProjectTD|Inventory", meta = (ArrayParm = "Requirements"))
    static bool TryRemoveItems(UActorComponent* InventoryComponent, const TArray<int32>& Requirements);
    DECLARE_FUNCTION(execTryRemoveItems);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool MoveItem(UObject* InventoryContext, int32 FromIndex, int32 ToIndex);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool SplitStack(UObject* InventoryContext, int32 SourceIndex, int32 SplitQuantity, int32& OutNewSlotIndex);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool DropItem(UObject* InventoryContext, int32 SlotIndex, int32 Quantity, FVector DropLocation, int32& QuantityDropped);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static TArray<FTDInventorySlotData> GetInventorySlots(UObject* InventoryContext);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Inventory")
    static TArray<FTDInventoryActionEntry> GetAvailableActionsForSlot(UObject* InventoryContext, int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool ExecuteItemAction(
        UObject* InventoryContext,
        int32 SlotIndex,
        ETDInventoryItemAction Action,
        int32 QuantityOverride,
        FVector DropLocation,
        int32& QuantityProcessed
    );

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Inventory")
    static bool TryUnequipItem(UObject* EquipmentContext, uint8 EquipSlot, int32& OutAddedSlotIndex);
};
