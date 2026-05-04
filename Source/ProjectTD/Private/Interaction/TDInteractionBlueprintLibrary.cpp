#include "Interaction/TDInteractionBlueprintLibrary.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace TDInteraction
{
    const FName OverlappingInteractablesPropertyName(TEXT("OverlappingInteractables"));
    const FName CurrentInteractTargetPropertyName(TEXT("CurrentInteractTarget"));
    const FName IsInteractingPropertyName(TEXT("bIsInteracting"));
    const FName OnInteractTargetChangedPropertyName(TEXT("OnInteractTargetChanged"));

    const FName BeginFocusFunctionName(TEXT("与物体开始接触"));
    const FName EndFocusFunctionName(TEXT("与物体结束接触"));
    const FName BeginInteractFunctionName(TEXT("交互开始"));
    const FName EndInteractFunctionName(TEXT("交互结束"));

    struct FInteractionComponentAccess
    {
        UObject* Object = nullptr;
        FArrayProperty* OverlappingInteractablesProperty = nullptr;
        FObjectProperty* CurrentInteractTargetProperty = nullptr;
        FBoolProperty* IsInteractingProperty = nullptr;
        FMulticastDelegateProperty* TargetChangedDelegateProperty = nullptr;
    };

    struct FInteractionResolveDebugInfo
    {
        bool bContextWasActor = false;
        bool bScannedComponents = false;
        FString SelectedComponentName;
        FString SelectedComponentClassName;
    };

    bool PropertyNameMatches(const FProperty* Property, const FName& ExactName, const TCHAR* FallbackToken)
    {
        return Property && (Property->GetFName() == ExactName || Property->GetName().Contains(FallbackToken, ESearchCase::IgnoreCase));
    }

    AActor* ResolveActor(UObject* Context)
    {
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

    FInteractionComponentAccess BuildAccessForObject(UObject* Object)
    {
        FInteractionComponentAccess Access;
        Access.Object = Object;
        if (!Access.Object)
        {
            return Access;
        }

        Access.OverlappingInteractablesProperty = FindFProperty<FArrayProperty>(Access.Object->GetClass(), OverlappingInteractablesPropertyName);
        Access.CurrentInteractTargetProperty = FindFProperty<FObjectProperty>(Access.Object->GetClass(), CurrentInteractTargetPropertyName);
        Access.IsInteractingProperty = FindFProperty<FBoolProperty>(Access.Object->GetClass(), IsInteractingPropertyName);
        Access.TargetChangedDelegateProperty = FindFProperty<FMulticastDelegateProperty>(Access.Object->GetClass(), OnInteractTargetChangedPropertyName);

        if (!Access.OverlappingInteractablesProperty)
        {
            for (TFieldIterator<FArrayProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FArrayProperty* Candidate = *It;
                if (PropertyNameMatches(Candidate, OverlappingInteractablesPropertyName, TEXT("Interact")))
                {
                    Access.OverlappingInteractablesProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.CurrentInteractTargetProperty)
        {
            for (TFieldIterator<FObjectProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FObjectProperty* Candidate = *It;
                const FString CandidateName = Candidate->GetName();
                if (CandidateName.Contains(TEXT("Current"), ESearchCase::IgnoreCase) &&
                    CandidateName.Contains(TEXT("Interact"), ESearchCase::IgnoreCase) &&
                    CandidateName.Contains(TEXT("Target"), ESearchCase::IgnoreCase))
                {
                    Access.CurrentInteractTargetProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.IsInteractingProperty)
        {
            for (TFieldIterator<FBoolProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FBoolProperty* Candidate = *It;
                if (PropertyNameMatches(Candidate, IsInteractingPropertyName, TEXT("Interacting")))
                {
                    Access.IsInteractingProperty = Candidate;
                    break;
                }
            }
        }

        if (!Access.TargetChangedDelegateProperty)
        {
            for (TFieldIterator<FMulticastDelegateProperty> It(Access.Object->GetClass()); It; ++It)
            {
                FMulticastDelegateProperty* Candidate = *It;
                const FString CandidateName = Candidate->GetName();
                if (CandidateName.Contains(TEXT("Interact"), ESearchCase::IgnoreCase) &&
                    CandidateName.Contains(TEXT("Target"), ESearchCase::IgnoreCase) &&
                    CandidateName.Contains(TEXT("Changed"), ESearchCase::IgnoreCase))
                {
                    Access.TargetChangedDelegateProperty = Candidate;
                    break;
                }
            }
        }

        const FObjectProperty* ArrayObjectProperty = Access.OverlappingInteractablesProperty
            ? CastField<FObjectProperty>(Access.OverlappingInteractablesProperty->Inner)
            : nullptr;
        if (!Access.OverlappingInteractablesProperty || !Access.CurrentInteractTargetProperty ||
            !ArrayObjectProperty || !ArrayObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()) ||
            !Access.CurrentInteractTargetProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
        {
            Access = FInteractionComponentAccess();
        }

        return Access;
    }

    bool IsPreferredInteractionComponentName(const UActorComponent* Component)
    {
        if (!IsValid(Component))
        {
            return false;
        }

        return Component->GetName().Contains(TEXT("AC_Interaction"), ESearchCase::IgnoreCase) ||
            Component->GetName().Contains(TEXT("Interaction"), ESearchCase::IgnoreCase) ||
            Component->GetClass()->GetName().Contains(TEXT("AC_Interaction"), ESearchCase::IgnoreCase) ||
            Component->GetClass()->GetName().Contains(TEXT("Interaction"), ESearchCase::IgnoreCase);
    }

    FInteractionComponentAccess ResolveInteraction(UObject* InteractionContext, FInteractionResolveDebugInfo* DebugInfo = nullptr)
    {
        if (!InteractionContext)
        {
            return FInteractionComponentAccess();
        }

        if (AActor* Actor = Cast<AActor>(InteractionContext))
        {
            if (DebugInfo)
            {
                DebugInfo->bContextWasActor = true;
                DebugInfo->bScannedComponents = true;
            }

            TInlineComponentArray<UActorComponent*> Components(Actor);

            for (UActorComponent* Component : Components)
            {
                FInteractionComponentAccess ComponentAccess = BuildAccessForObject(Component);
                if (ComponentAccess.Object && IsPreferredInteractionComponentName(Component))
                {
                    if (DebugInfo)
                    {
                        DebugInfo->SelectedComponentName = Component->GetName();
                        DebugInfo->SelectedComponentClassName = Component->GetClass()->GetName();
                    }
                    return ComponentAccess;
                }
            }

            for (UActorComponent* Component : Components)
            {
                FInteractionComponentAccess ComponentAccess = BuildAccessForObject(Component);
                if (ComponentAccess.Object)
                {
                    if (DebugInfo)
                    {
                        DebugInfo->SelectedComponentName = Component->GetName();
                        DebugInfo->SelectedComponentClassName = Component->GetClass()->GetName();
                    }
                    return ComponentAccess;
                }
            }

            // Actor-owned variables are only a fallback. AC_Interaction should win whenever it exists.
            return BuildAccessForObject(Actor);
        }

        FInteractionComponentAccess DirectAccess = BuildAccessForObject(InteractionContext);
        if (DirectAccess.Object)
        {
            return DirectAccess;
        }

        for (TFieldIterator<FObjectProperty> It(InteractionContext->GetClass()); It; ++It)
        {
            FObjectProperty* ObjectProperty = *It;
            UObject* ReferencedObject = ObjectProperty->GetObjectPropertyValue_InContainer(InteractionContext);
            FInteractionComponentAccess ReferencedAccess = BuildAccessForObject(ReferencedObject);
            if (ReferencedAccess.Object)
            {
                return ReferencedAccess;
            }
        }

        return FInteractionComponentAccess();
    }

    bool CallInteractableEvent(AActor* TargetActor, FName FunctionName, AActor* Interactor)
    {
        if (!IsValid(TargetActor))
        {
            return false;
        }

        UFunction* Function = TargetActor->FindFunction(FunctionName);
        if (!Function)
        {
            return false;
        }

        TArray<uint8> Params;
        Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());

        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                continue;
            }

            if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
            {
                if (Interactor && ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
                {
                    ObjectProperty->SetObjectPropertyValue_InContainer(Params.GetData(), Interactor);
                }
            }
        }

        TargetActor->ProcessEvent(Function, Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return true;
    }

    bool IsInteractable(AActor* Actor)
    {
        return IsValid(Actor) &&
            (Actor->FindFunction(BeginFocusFunctionName) ||
                Actor->FindFunction(EndFocusFunctionName) ||
                Actor->FindFunction(BeginInteractFunctionName) ||
                Actor->FindFunction(EndInteractFunctionName));
    }

    AActor* GetCurrentTarget(const FInteractionComponentAccess& Access)
    {
        if (!Access.Object || !Access.CurrentInteractTargetProperty)
        {
            return nullptr;
        }

        return Cast<AActor>(Access.CurrentInteractTargetProperty->GetObjectPropertyValue_InContainer(Access.Object));
    }

    void SetCurrentTarget(const FInteractionComponentAccess& Access, AActor* NewTarget)
    {
        if (Access.Object && Access.CurrentInteractTargetProperty)
        {
            Access.CurrentInteractTargetProperty->SetObjectPropertyValue_InContainer(Access.Object, NewTarget);
        }
    }

    void SetIsInteracting(const FInteractionComponentAccess& Access, bool bIsInteracting)
    {
        if (Access.Object && Access.IsInteractingProperty)
        {
            Access.IsInteractingProperty->SetPropertyValue_InContainer(Access.Object, bIsInteracting);
        }
    }

    bool GetIsInteracting(const FInteractionComponentAccess& Access)
    {
        return Access.Object && Access.IsInteractingProperty
            ? Access.IsInteractingProperty->GetPropertyValue_InContainer(Access.Object)
            : false;
    }

    void BroadcastTargetChanged(const FInteractionComponentAccess& Access, AActor* NewTarget)
    {
        if (!Access.Object || !Access.TargetChangedDelegateProperty)
        {
            return;
        }

        FMulticastScriptDelegate* ScriptDelegate = Access.TargetChangedDelegateProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Access.Object);
        if (!ScriptDelegate)
        {
            return;
        }

        UFunction* SignatureFunction = Access.TargetChangedDelegateProperty->SignatureFunction;
        if (!SignatureFunction || SignatureFunction->ParmsSize <= 0)
        {
            ScriptDelegate->ProcessMulticastDelegate<UObject>(nullptr);
            return;
        }

        TArray<uint8> Params;
        Params.SetNumZeroed(SignatureFunction->ParmsSize);
        SignatureFunction->InitializeStruct(Params.GetData());

        for (TFieldIterator<FProperty> It(SignatureFunction); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                continue;
            }

            if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
            {
                if (ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
                {
                    ObjectProperty->SetObjectPropertyValue_InContainer(Params.GetData(), NewTarget);
                    break;
                }
            }
        }

        ScriptDelegate->ProcessMulticastDelegate<UObject>(Params.GetData());
        SignatureFunction->DestroyStruct(Params.GetData());
    }

    FScriptArrayHelper MakeOverlapHelper(const FInteractionComponentAccess& Access)
    {
        return FScriptArrayHelper(Access.OverlappingInteractablesProperty, Access.OverlappingInteractablesProperty->ContainerPtrToValuePtr<void>(Access.Object));
    }

    AActor* ReadOverlapAt(const FInteractionComponentAccess& Access, FScriptArrayHelper& Helper, int32 Index)
    {
        FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Access.OverlappingInteractablesProperty->Inner);
        return ObjectProperty ? Cast<AActor>(ObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index))) : nullptr;
    }

    void WriteOverlapAt(const FInteractionComponentAccess& Access, FScriptArrayHelper& Helper, int32 Index, AActor* Actor)
    {
        FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Access.OverlappingInteractablesProperty->Inner);
        if (ObjectProperty)
        {
            ObjectProperty->SetObjectPropertyValue(Helper.GetRawPtr(Index), Actor);
        }
    }
}

bool UTDInteractionBlueprintLibrary::AddInteractable(UObject* InteractionContext, AActor* InteractableActor, AActor* Interactor)
{
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext);
    if (!Access.Object || !InteractableActor || !TDInteraction::IsInteractable(InteractableActor))
    {
        return false;
    }

    FScriptArrayHelper Helper = TDInteraction::MakeOverlapHelper(Access);
    for (int32 Index = 0; Index < Helper.Num(); ++Index)
    {
        if (TDInteraction::ReadOverlapAt(Access, Helper, Index) == InteractableActor)
        {
            UpdateCurrentInteractTarget(InteractionContext, Interactor);
            return true;
        }
    }

    const int32 NewIndex = Helper.AddValue();
    TDInteraction::WriteOverlapAt(Access, Helper, NewIndex, InteractableActor);
    UpdateCurrentInteractTarget(InteractionContext, Interactor);
    return true;
}

bool UTDInteractionBlueprintLibrary::RemoveInteractable(UObject* InteractionContext, AActor* InteractableActor, AActor* Interactor)
{
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext);
    if (!Access.Object || !InteractableActor)
    {
        return false;
    }

    bool bRemoved = false;
    FScriptArrayHelper Helper = TDInteraction::MakeOverlapHelper(Access);
    for (int32 Index = Helper.Num() - 1; Index >= 0; --Index)
    {
        if (TDInteraction::ReadOverlapAt(Access, Helper, Index) == InteractableActor)
        {
            Helper.RemoveValues(Index);
            bRemoved = true;
        }
    }

    UpdateCurrentInteractTarget(InteractionContext, Interactor);
    return bRemoved;
}

AActor* UTDInteractionBlueprintLibrary::UpdateCurrentInteractTarget(UObject* InteractionContext, AActor* Interactor)
{
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext);
    if (!Access.Object)
    {
        return nullptr;
    }

    AActor* OldTarget = TDInteraction::GetCurrentTarget(Access);
    AActor* BestTarget = nullptr;
    float BestScore = -FLT_MAX;

    FScriptArrayHelper Helper = TDInteraction::MakeOverlapHelper(Access);
    const FVector InteractorLocation = Interactor ? Interactor->GetActorLocation() : FVector::ZeroVector;
    const FVector InteractorForward = Interactor ? Interactor->GetActorForwardVector() : FVector::ForwardVector;

    for (int32 Index = Helper.Num() - 1; Index >= 0; --Index)
    {
        AActor* Candidate = TDInteraction::ReadOverlapAt(Access, Helper, Index);
        if (!IsValid(Candidate) || !TDInteraction::IsInteractable(Candidate))
        {
            Helper.RemoveValues(Index);
            continue;
        }

        if (!Interactor)
        {
            if (!BestTarget)
            {
                BestTarget = Candidate;
            }
            continue;
        }

        const FVector ToCandidate = Candidate->GetActorLocation() - InteractorLocation;
        const float Distance = ToCandidate.Size();
        const FVector Direction = Distance > UE_SMALL_NUMBER ? ToCandidate / Distance : InteractorForward;
        const float Dot = FVector::DotProduct(InteractorForward, Direction);
        if (Dot < 0.0f)
        {
            continue;
        }

        const float Score = Dot * 1000.0f - Distance;
        if (Score > BestScore)
        {
            BestScore = Score;
            BestTarget = Candidate;
        }
    }

    if (BestTarget != OldTarget)
    {
        if (IsValid(OldTarget))
        {
            TDInteraction::CallInteractableEvent(OldTarget, TDInteraction::EndFocusFunctionName, Interactor);
        }

        TDInteraction::SetCurrentTarget(Access, BestTarget);

        if (IsValid(BestTarget))
        {
            TDInteraction::CallInteractableEvent(BestTarget, TDInteraction::BeginFocusFunctionName, Interactor);
        }

        TDInteraction::BroadcastTargetChanged(Access, BestTarget);
    }

    return BestTarget;
}

AActor* UTDInteractionBlueprintLibrary::GetCurrentInteractTarget(UObject* InteractionContext)
{
    return TDInteraction::GetCurrentTarget(TDInteraction::ResolveInteraction(InteractionContext));
}

bool UTDInteractionBlueprintLibrary::BeginInteract(UObject* InteractionContext, AActor* Interactor)
{
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext);
    AActor* Target = TDInteraction::GetCurrentTarget(Access);
    if (!Access.Object || !TDInteraction::IsInteractable(Target))
    {
        return false;
    }

    TDInteraction::SetIsInteracting(Access, true);
    return TDInteraction::CallInteractableEvent(Target, TDInteraction::BeginInteractFunctionName, Interactor);
}

bool UTDInteractionBlueprintLibrary::EndInteract(UObject* InteractionContext, AActor* Interactor)
{
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext);
    if (!Access.Object)
    {
        return false;
    }

    AActor* Target = TDInteraction::GetCurrentTarget(Access);
    const bool bCalledTarget = TDInteraction::IsInteractable(Target) &&
        TDInteraction::CallInteractableEvent(Target, TDInteraction::EndInteractFunctionName, Interactor);

    TDInteraction::SetIsInteracting(Access, false);
    return bCalledTarget;
}

FString UTDInteractionBlueprintLibrary::GetInteractionDebugSummary(UObject* InteractionContext, AActor* Interactor)
{
    TDInteraction::FInteractionResolveDebugInfo ResolveDebug;
    const TDInteraction::FInteractionComponentAccess Access = TDInteraction::ResolveInteraction(InteractionContext, &ResolveDebug);
    if (!InteractionContext)
    {
        return TEXT("InteractionContext=null");
    }

    FString Summary = FString::Printf(
        TEXT("Context=%s Class=%s, Resolved=%s Class=%s"),
        *InteractionContext->GetName(),
        *InteractionContext->GetClass()->GetName(),
        Access.Object ? *Access.Object->GetName() : TEXT("null"),
        Access.Object ? *Access.Object->GetClass()->GetName() : TEXT("null")
    );

    Summary += FString::Printf(
        TEXT(", ContextIsActor=%s, ScannedComponents=%s, SelectedComponent=%s Class=%s"),
        ResolveDebug.bContextWasActor ? TEXT("true") : TEXT("false"),
        ResolveDebug.bScannedComponents ? TEXT("true") : TEXT("false"),
        ResolveDebug.SelectedComponentName.IsEmpty() ? TEXT("null") : *ResolveDebug.SelectedComponentName,
        ResolveDebug.SelectedComponentClassName.IsEmpty() ? TEXT("null") : *ResolveDebug.SelectedComponentClassName
    );

    Summary += FString::Printf(
        TEXT(", OverlapsProp=%s, CurrentTargetProp=%s, IsInteractingProp=%s, Delegate=%s"),
        Access.OverlappingInteractablesProperty ? *Access.OverlappingInteractablesProperty->GetName() : TEXT("null"),
        Access.CurrentInteractTargetProperty ? *Access.CurrentInteractTargetProperty->GetName() : TEXT("null"),
        Access.IsInteractingProperty ? *Access.IsInteractingProperty->GetName() : TEXT("null"),
        Access.TargetChangedDelegateProperty ? *Access.TargetChangedDelegateProperty->GetName() : TEXT("null")
    );

    if (!Access.Object || !Access.OverlappingInteractablesProperty)
    {
        return Summary;
    }

    AActor* CurrentTarget = TDInteraction::GetCurrentTarget(Access);
    Summary += FString::Printf(
        TEXT(", CurrentTarget=%s, bIsInteracting=%s"),
        CurrentTarget ? *CurrentTarget->GetName() : TEXT("null"),
        TDInteraction::GetIsInteracting(Access) ? TEXT("true") : TEXT("false")
    );

    const FVector InteractorLocation = Interactor ? Interactor->GetActorLocation() : FVector::ZeroVector;
    const FVector InteractorForward = Interactor ? Interactor->GetActorForwardVector() : FVector::ForwardVector;
    Summary += FString::Printf(TEXT(", Forward=%s"), *InteractorForward.ToCompactString());

    FScriptArrayHelper Helper = TDInteraction::MakeOverlapHelper(Access);
    Summary += FString::Printf(TEXT(", OverlapCount=%d"), Helper.Num());
    for (int32 Index = 0; Index < Helper.Num(); ++Index)
    {
        AActor* Candidate = TDInteraction::ReadOverlapAt(Access, Helper, Index);
        const bool bValid = IsValid(Candidate);
        const bool bInteractable = TDInteraction::IsInteractable(Candidate);
        float Dot = 0.0f;
        float Distance = 0.0f;
        float Score = 0.0f;

        if (bValid && Interactor)
        {
            const FVector ToCandidate = Candidate->GetActorLocation() - InteractorLocation;
            Distance = ToCandidate.Size();
            const FVector Direction = Distance > UE_SMALL_NUMBER ? ToCandidate / Distance : InteractorForward;
            Dot = FVector::DotProduct(InteractorForward, Direction);
            Score = Dot * 1000.0f - Distance;
        }

        Summary += FString::Printf(
            TEXT(" [%d:%s Valid=%s Interactable=%s Dot=%.2f Distance=%.1f Score=%.1f]"),
            Index,
            Candidate ? *Candidate->GetName() : TEXT("null"),
            bValid ? TEXT("true") : TEXT("false"),
            bInteractable ? TEXT("true") : TEXT("false"),
            Dot,
            Distance,
            Score
        );
    }

    return Summary;
}
