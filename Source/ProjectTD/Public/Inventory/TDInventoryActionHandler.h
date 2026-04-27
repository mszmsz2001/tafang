#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TDInventoryActionHandler.generated.h"

UINTERFACE(BlueprintType)
class PROJECTTD_API UTDInventoryActionHandler : public UInterface
{
    GENERATED_BODY()
};

class PROJECTTD_API ITDInventoryActionHandler
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ProjectTD|Inventory")
    bool HandleInventoryUseItem(FName ItemID, int32 SlotIndex, int32 Quantity);
};
