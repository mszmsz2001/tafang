#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "TDInteractionBlueprintLibrary.generated.h"

UCLASS()
class PROJECTTD_API UTDInteractionBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static bool AddInteractable(UObject* InteractionContext, AActor* InteractableActor, AActor* Interactor);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static bool RemoveInteractable(UObject* InteractionContext, AActor* InteractableActor, AActor* Interactor);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static AActor* UpdateCurrentInteractTarget(UObject* InteractionContext, AActor* Interactor);

    UFUNCTION(BlueprintPure, Category = "ProjectTD|Interaction")
    static AActor* GetCurrentInteractTarget(UObject* InteractionContext);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static bool BeginInteract(UObject* InteractionContext, AActor* Interactor);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static bool EndInteract(UObject* InteractionContext, AActor* Interactor);

    UFUNCTION(BlueprintCallable, Category = "ProjectTD|Interaction")
    static FString GetInteractionDebugSummary(UObject* InteractionContext, AActor* Interactor);
};
