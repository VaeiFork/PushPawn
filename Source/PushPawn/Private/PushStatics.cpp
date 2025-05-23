// Copyright (c) Jared Taylor. All Rights Reserved

#include "PushStatics.h"

#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Abilities/PushPawnAbilityTargetData.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PusheeComponent.h"
#include "Components/PusherComponent.h"
#include "Components/SphereComponent.h"
#include "IPush.h"
#include "PushQuery.h"

#include "Engine/OverlapResult.h"
#include "Curves/CurveFloat.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PushStatics)

namespace FPushPawnCVars
{
#if !UE_BUILD_SHIPPING
	static bool bPushPawnVelocityStrengthScalarDisabled = false;
	FAutoConsoleVariableRef CVarPushPawnDisableVelocityStrengthScalar(
		TEXT("p.PushPawn.DisableVelocityScaling"),
		bPushPawnVelocityStrengthScalarDisabled,
		TEXT("Disable PushPawn velocity based strength scalar.\n"),
		ECVF_Cheat);
	
	static bool bPushPawnDistanceStrengthScalarDisabled = false;
	FAutoConsoleVariableRef CVarPushPawnDisableDistanceStrengthScalar(
		TEXT("p.PushPawn.DisableDistanceScaling"),
		bPushPawnDistanceStrengthScalarDisabled,
		TEXT("Disable PushPawn distance based strength scalar.\n"),
		ECVF_Cheat);
#endif
}

float SafeDivide(float A, float B)
{
	// Prevent potential divide by zero issues
	return B != 0.f ? (A / B) : 0.f;
}

void UPushStatics::GetPushActorsFromEventData(const FGameplayEventData& EventData, const AActor*& Pushee, const AActor*& Pusher)
{
	Pushee = EventData.Instigator.Get();
	Pusher = EventData.Target.Get();
}

void UPushStatics::K2_GetPusherPawnFromEventData(AActor*& Pusher, const FGameplayEventData& EventData,
	TSubclassOf<APawn> PawnClass)
{
	// Pusher is the target
	if (PawnClass)
	{
		Pusher = const_cast<AActor*>(EventData.Target.Get());
	}
	else
	{
		Pusher = nullptr;
	}
}

void UPushStatics::K2_GetPusheePawnFromEventData(AActor*& Pushee, const FGameplayEventData& EventData,
	TSubclassOf<APawn> PawnClass)
{
	// Pushee is the instigator
	if (PawnClass)
	{
		Pushee = const_cast<AActor*>(EventData.Instigator.Get());
	}
	else
	{
		Pushee = nullptr;
	}
}

void UPushStatics::GetPushDataFromEventData(const FGameplayEventData& EventData, bool bForce2D, FVector& PushDirection, float& DistanceBetween, float&
	StrengthScalar, bool& bOverrideStrength)
{
	// Get the target data from the event data
	const FGameplayAbilityTargetData* RawData = EventData.TargetData.Get(0);
	check(RawData);
	const FPushPawnAbilityTargetData& PushTargetData = static_cast<const FPushPawnAbilityTargetData&>(*RawData);

	// Normalize the direction
	if (bForce2D)
	{
		PushDirection = PushTargetData.Direction.GetSafeNormal2D();
	}
	else
	{
		PushDirection = PushTargetData.Direction.GetSafeNormal();
	}

	// Extract distance
	DistanceBetween = PushTargetData.Distance;

	// Get the strength target data from the event data if available
	if (const FGameplayAbilityTargetData* RawStrengthData = EventData.TargetData.Get(1))
	{
		const FPushPawnStrengthTargetData& PushStrengthTargetData = static_cast<const FPushPawnStrengthTargetData&>(*RawStrengthData);
		
		// Extract strength scalar
		StrengthScalar = PushStrengthTargetData.StrengthScalar;

		// Extract strength override
		bOverrideStrength = PushStrengthTargetData.bOverrideStrength;
	}
	else
	{
		StrengthScalar = 1.f;
		bOverrideStrength = false;
	}
}

bool UPushStatics::GetDefaultCapsuleRootComponent(const AActor* Actor, float& CapsuleRadius, float& CapsuleHalfHeight)
{
	CapsuleRadius = 0.f;
	CapsuleHalfHeight = 0.f;
	
	const AActor* DefaultActor = Actor ? Actor->GetClass()->GetDefaultObject<AActor>() : nullptr;
	if (DefaultActor && DefaultActor->GetRootComponent())
	{
		if (const UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(DefaultActor->GetRootComponent()))
		{
			CapsuleRadius = CapsuleComponent->GetUnscaledCapsuleRadius();
			CapsuleHalfHeight = CapsuleComponent->GetUnscaledCapsuleHalfHeight();
			return true;
		}
	}
	return false;
}

bool UPushStatics::IsPawnMovingOnGround(const APawn* Pawn)
{
	return Pawn->GetMovementComponent() ? Pawn->GetMovementComponent()->IsMovingOnGround() : false;
}

bool UPushStatics::IsPusheeMovingOnGround(const IPusheeInstigator* Pushee)
{
	return Pushee->IsPusheeMovingOnGround();
}

FVector UPushStatics::GetPawnGroundVelocity(const APawn* Pawn)
{
	// Factor incline into the velocity when on the ground
	const FVector& Velocity = Pawn->GetVelocity();
	return IsPawnMovingOnGround(Pawn) ? Velocity : FVector(Velocity.X, Velocity.Y, 0.f);
}

FVector UPushStatics::GetPusheeGroundVelocity(const IPusheeInstigator* Pushee)
{
	const FVector& Velocity = Pushee->GetPusheeVelocity();
	return Pushee->IsPusheeMovingOnGround() ? Velocity : FVector(Velocity.X, Velocity.Y, 0.f);
}

float UPushStatics::GetPawnGroundSpeed(const APawn* Pawn)
{
	// Factor incline into the velocity when on the ground
	return GetPawnGroundVelocity(Pawn).Size();
}

float UPushStatics::GetPusheeGroundSpeed(const IPusheeInstigator* Pushee)
{
	// Factor incline into the velocity when on the ground
	return GetPusheeGroundVelocity(Pushee).Size();
}

float UPushStatics::GetNormalizedPushDistance(const AActor* Pushee, const AActor* Pusher, float DistanceBetween)
{
	const float CombinedRadius = Pushee->GetSimpleCollisionRadius() + Pusher->GetSimpleCollisionRadius();
	const float NormalizedDistance = SafeDivide(DistanceBetween, CombinedRadius);
	return NormalizedDistance;
}

float UPushStatics::GetPushStrength(const APawn* Pushee, float Distance, const FPushPawnActionParams& Params)
{
	// Get the strength from the curve and apply the scalar
	float Strength = Params.StrengthScalar;
	
	// Scale strength based on pushee velocity
	if (Params.VelocityToStrengthCurve)
	{
		bool bEvaluateVelocityToStrengthCurve = true;

#if !UE_BUILD_SHIPPING
		if (FPushPawnCVars::bPushPawnVelocityStrengthScalarDisabled)
		{
			bEvaluateVelocityToStrengthCurve = false;
		}
#endif

		if (bEvaluateVelocityToStrengthCurve)
		{
			// Get the speed of the pushee
			const float PusheeSpeed = GetPawnGroundSpeed(Pushee);

			// Get the strength from the curve and apply the scalar
			Strength *= Params.VelocityToStrengthCurve->GetFloatValue(PusheeSpeed);	
		}
	}

	// Scale strength based on distance between the pushee & pusher
	if (Params.DistanceToStrengthCurve)
	{
		bool bEvaluateDistanceToStrengthCurve = true;

#if !UE_BUILD_SHIPPING
		if (FPushPawnCVars::bPushPawnDistanceStrengthScalarDisabled)
		{
			bEvaluateDistanceToStrengthCurve = false;
		}
#endif
		
		if (bEvaluateDistanceToStrengthCurve)
		{
			Strength *= Params.DistanceToStrengthCurve->GetFloatValue(Distance);	
		}
	}

	return Strength;
}

float UPushStatics::CalculatePushStrength(const APawn* Pushee, bool bOverrideStrength, float StrengthScalar,
	float NormalizedDistance, const FPushPawnActionParams& Params)
{
	if (bOverrideStrength)
	{
		return StrengthScalar;
	}
	return GetPushStrength(Pushee, NormalizedDistance, Params) * StrengthScalar;
}

float UPushStatics::GetPushStrengthSimple(const APawn* Pushee, const UCurveFloat* VelocityToStrengthCurve, const UCurveFloat* DistanceToStrengthCurve, float Distance, float StrengthScalar)
{
	float Strength = StrengthScalar;
	 
	// Scale strength based on pushee velocity
	if (VelocityToStrengthCurve)
	{
		bool bEvaluateVelocityToStrengthCurve = true;

#if !UE_BUILD_SHIPPING
		if (FPushPawnCVars::bPushPawnVelocityStrengthScalarDisabled)
		{
			bEvaluateVelocityToStrengthCurve = false;
		}
#endif

		if (bEvaluateVelocityToStrengthCurve)
		{
			// Get the speed of the pushee
			const float PusheeSpeed = GetPawnGroundSpeed(Pushee);

			// Get the strength from the curve and apply the scalar
			Strength *= VelocityToStrengthCurve->GetFloatValue(PusheeSpeed);
		}
	}

	// Scale strength based on distance between the pushee & pusher
	if (DistanceToStrengthCurve)
	{
		bool bEvaluateDistanceToStrengthCurve = true;

#if !UE_BUILD_SHIPPING
		if (FPushPawnCVars::bPushPawnDistanceStrengthScalarDisabled)
		{
			bEvaluateDistanceToStrengthCurve = false;
		}
#endif

		if (bEvaluateDistanceToStrengthCurve)
		{
			Strength *= DistanceToStrengthCurve->GetFloatValue(Distance);	
		}
	}

	return Strength;
}

float UPushStatics::CalculatePushDirection(const FVector& Direction, const FRotator& BaseRotation)
{
	if (!Direction.IsNearlyZero())
	{
		const FMatrix RotMatrix = FRotationMatrix(BaseRotation);
		const FVector ForwardVector = RotMatrix.GetScaledAxis(EAxis::X);
		const FVector RightVector = RotMatrix.GetScaledAxis(EAxis::Y);
		const FVector NormalizedVel = Direction.GetSafeNormal2D();

		// get a cos(alpha) of forward vector vs velocity
		const float ForwardCosAngle = FVector::DotProduct(ForwardVector, NormalizedVel);
		// now get the alpha and convert to degree
		float ForwardDeltaDegree = FMath::RadiansToDegrees(FMath::Acos(ForwardCosAngle));

		// depending on where right vector is, flip it
		const float RightCosAngle = FVector::DotProduct(RightVector, NormalizedVel);
		if (RightCosAngle < 0)
		{
			ForwardDeltaDegree *= -1;
		}

		return ForwardDeltaDegree;
	}

	return 0.f;
}

EPushCardinal_4Way UPushStatics::GetPushDirection_4Way(const AActor* FromActor, const AActor* ToActor,
	EValidPushDirection& ValidPushDirection)
{ 
	ValidPushDirection = EValidPushDirection::InvalidDirection;

	// Get the direction from the pushee to the pusher
	const FVector Direction = (FromActor->GetActorLocation() - ToActor->GetActorLocation()).GetSafeNormal2D();

	// If the direction is nearly zero, default to forward
	if (Direction.IsNearlyZero())
	{
		return EPushCardinal_4Way::Forward;
	}

	ValidPushDirection = EValidPushDirection::ValidDirection;

	const float Rotation = CalculatePushDirection(Direction, ToActor->GetActorRotation());
	const float RotationAbs = FMath::Abs(Rotation);

	// Left or Right
	if (RotationAbs >= 45.f && RotationAbs <= 135.f)
	{
		return Rotation > 0.f ? EPushCardinal_4Way::Right : EPushCardinal_4Way::Left;
	}

	// Forward
	if (RotationAbs <= 45.f)
	{
		return EPushCardinal_4Way::Forward;
	}

	// Backward
	return EPushCardinal_4Way::Backward;
}

EPushCardinal_8Way UPushStatics::GetPushDirection_8Way(const AActor* FromActor, const AActor* ToActor,
	EValidPushDirection& ValidPushDirection)
{
	ValidPushDirection = EValidPushDirection::InvalidDirection;

	// Get the direction from the pushee to the pusher
	const FVector Direction = (FromActor->GetActorLocation() - ToActor->GetActorLocation()).GetSafeNormal2D();

	// If the direction is nearly zero, default to forward
	if (Direction.IsNearlyZero())
	{
		return EPushCardinal_8Way::Forward;
	}

	ValidPushDirection = EValidPushDirection::ValidDirection;

	const float Rotation = CalculatePushDirection(Direction, ToActor->GetActorRotation());
	const float RotationAbs = FMath::Abs(Rotation);

	// Left or Right
	if (RotationAbs >= 67.5 && RotationAbs <= 112.5)
	{
		return Rotation > 0.f ? EPushCardinal_8Way::Right : EPushCardinal_8Way::Left;
	}

	// Forward
	if (RotationAbs <= 22.5f)
	{
		return EPushCardinal_8Way::Forward;
	}
	
	// Backward
	if (RotationAbs >= 157.5f)
	{
		return EPushCardinal_8Way::Backward;
	}

	// ForwardLeft or ForwardRight
	if (RotationAbs <= 67.5f)
	{
		return Rotation > 0.f ? EPushCardinal_8Way::ForwardRight : EPushCardinal_8Way::ForwardLeft;
	}

	// BackwardLeft or BackwardRight
	if (RotationAbs >= 112.5f)
	{
		return Rotation > 0.f ? EPushCardinal_8Way::BackwardRight : EPushCardinal_8Way::BackwardLeft;
	}

	// Default to forward
	return EPushCardinal_8Way::Forward;
}

IPusheeInstigator* UPushStatics::GetPusheeInstigator(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	if (IPusheeInstigator* Interface = Cast<IPusheeInstigator>(Actor))
	{
		return Interface;
	}

	if (auto* Component = Actor->GetComponentByClass<UPusheeComponent>())
	{
		return Cast<IPusheeInstigator>(Component);
	}

	return nullptr;
}

const IPusheeInstigator* UPushStatics::GetPusheeInstigator(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	if (const IPusheeInstigator* Interface = Cast<IPusheeInstigator>(Actor))
	{
		return Interface;
	}

	if (auto* Component = Actor->GetComponentByClass<UPusheeComponent>())
	{
		return Cast<IPusheeInstigator>(Component);
	}

	return nullptr;
}

IPusherTarget* UPushStatics::GetPusherTarget(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	if (IPusherTarget* Interface = Cast<IPusherTarget>(Actor))
	{
		return Interface;
	}

	if (auto* Component = Actor->GetComponentByClass<UPusherComponent>())
	{
		return Cast<IPusherTarget>(Component);
	}

	return nullptr;
}

const IPusherTarget* UPushStatics::GetPusherTarget(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	if (const IPusherTarget* Interface = Cast<IPusherTarget>(Actor))
	{
		return Interface;
	}

	if (auto* Component = Actor->GetComponentByClass<UPusherComponent>())
	{
		return Cast<IPusherTarget>(Component);
	}

	return nullptr;
}

FVector UPushStatics::GetPushPawnAcceleration(const IPusheeInstigator* Pushee)
{
	return Pushee ? Pushee->GetPusheeAcceleration().GetSafeNormal() : FVector::ZeroVector;
}

FVector UPushStatics::GetPushPawnAcceleration(APawn* Pushee)
{
	const IPusheeInstigator* PusheeInstigator = GetPusheeInstigator(Pushee);
	return PusheeInstigator ? PusheeInstigator->GetPusheeAcceleration().GetSafeNormal() : FVector::ZeroVector;
}

bool UPushStatics::IsPusheeAccelerating(const IPusheeInstigator* Pushee)
{
	return IsPusheeAccelerating(GetPushPawnAcceleration(Pushee));
}

bool UPushStatics::IsPusheeAccelerating(APawn* Pushee)
{
	return IsPusheeAccelerating(GetPushPawnAcceleration(Pushee));
}

bool UPushStatics::IsPusheeAccelerating(const FVector& Acceleration)
{
	return !Acceleration.GetSafeNormal().IsNearlyZero(0.1f);
}

const float& UPushStatics::GetPushPawnScanRate(const IPusheeInstigator* Pushee, const FPushPawnScanParams& ScanParams)
{
	return GetPushPawnScanRate(GetPushPawnAcceleration(Pushee), ScanParams);
}

const float& UPushStatics::GetPushPawnScanRate(APawn* Pushee, const FPushPawnScanParams& ScanParams)
{
	return GetPushPawnScanRate(GetPushPawnAcceleration(Pushee), ScanParams);
}

const float& UPushStatics::GetPushPawnScanRate(const FVector& Acceleration, const FPushPawnScanParams& ScanParams)
{
	return IsPusheeAccelerating(Acceleration) ? ScanParams.ScanRateAccel : ScanParams.ScanRate;
}

float UPushStatics::GetPushPawnScanRange(const IPusheeInstigator* Pushee, float BaseScanRange,
	const FPushPawnScanParams& ScanParams)
{
	return GetPushPawnScanRange(GetPushPawnAcceleration(Pushee), BaseScanRange, ScanParams);
}

float UPushStatics::GetPushPawnScanRange(APawn* Pushee, float BaseScanRange,
	const FPushPawnScanParams& ScanParams)
{
	return GetPushPawnScanRange(GetPushPawnAcceleration(Pushee), BaseScanRange, ScanParams);
}

float UPushStatics::GetPushPawnScanRange(const FVector& Acceleration, float BaseScanRange,
	const FPushPawnScanParams& ScanParams)
{
	// If the pushee is accelerating, return the scalar, otherwise return the base rate
	return BaseScanRange * (IsPusheeAccelerating(Acceleration) ? ScanParams.ScanRangeAccelScalar : ScanParams.ScanRangeScalar);
}

bool UPushStatics::GatherPushOptions(const TSubclassOf<UGameplayAbility>& PushAbilityToGrant,
	const APawn* PusherPawn, const FPushQuery& PushQuery, const FPushOptionBuilder& OptionBuilder)
{
	if (PushAbilityToGrant && PusherPawn && PushQuery.RequestingAvatar.IsValid())
	{
		FPushOption Push;
		Push.PushAbilityToGrant = PushAbilityToGrant;
		Push.PusheeActorLocation = PushQuery.RequestingAvatar->GetActorLocation();
		Push.PusheeForwardVector = PusherPawn->GetActorForwardVector();
		Push.PusherActorLocation = PusherPawn->GetActorLocation();
		OptionBuilder.AddPushOption(Push);
		return true;
	}
	return false;
}

EPushCollisionType UPushStatics::GetPusheeCollisionShapeType(const AActor* Actor)
{
	const USceneComponent* RootComponent = Actor->GetRootComponent();
	if (RootComponent->IsA<UCapsuleComponent>())
	{
		return EPushCollisionType::Capsule;
	}
	else if (RootComponent->IsA<UBoxComponent>())
	{
		return EPushCollisionType::Box;
	}
	else if (RootComponent->IsA<USphereComponent>())
	{
		return EPushCollisionType::Sphere;
	}
	return EPushCollisionType::None;
}

FCollisionShape UPushStatics::GetDefaultPusheeCollisionShape(const AActor* Actor, FQuat& OutShapeRotation, EPushCollisionType OptionalShapeType, USceneComponent* OptionalComponent)
{
	if (OptionalShapeType == EPushCollisionType::None)
	{
		OptionalShapeType = GetPusheeCollisionShapeType(Actor);
	}

	// Use the default root component if no specific component is supplied
	USceneComponent* Component = OptionalComponent ? OptionalComponent : Actor->GetClass()->GetDefaultObject<AActor>()->GetRootComponent();
	OutShapeRotation = Component->GetComponentQuat();
	switch (OptionalShapeType)
	{
		case EPushCollisionType::Capsule:
		{
			const UCapsuleComponent* CapsuleComponent = CastChecked<UCapsuleComponent>(Component);
			const float Radius = CapsuleComponent->GetScaledCapsuleRadius();
			const float HalfHeight = FMath::Max<float>(Radius, CapsuleComponent->GetScaledCapsuleHalfHeight());
			return FCollisionShape::MakeCapsule(Radius, HalfHeight);
		}
		case EPushCollisionType::Box:
		{
			const UBoxComponent* BoxComponent = CastChecked<UBoxComponent>(Component);
			return FCollisionShape::MakeBox(BoxComponent->GetScaledBoxExtent());
		}
		case EPushCollisionType::Sphere:
		{
			const USphereComponent* SphereComponent = CastChecked<USphereComponent>(Component);
			return FCollisionShape::MakeSphere(SphereComponent->GetScaledSphereRadius());
		}
		default: return {};
	}
}

float UPushStatics::GetMaxDefaultCollisionShapeSize(const AActor* Actor, EPushCollisionType SpecificShapeType)
{
	if (Actor)
	{
		FQuat ShapeRotation;
		
		const FCollisionShape CollisionShape = GetDefaultPusheeCollisionShape(Actor, ShapeRotation, SpecificShapeType, nullptr);
		if (!CollisionShape.IsNearlyZero())
		{
			switch (CollisionShape.ShapeType)
			{
				case ECollisionShape::Box: return CollisionShape.GetExtent().GetAbsMax();
				case ECollisionShape::Sphere: return CollisionShape.GetSphereRadius();
				case ECollisionShape::Capsule: return FMath::Max<float>(CollisionShape.GetCapsuleRadius(), CollisionShape.GetCapsuleHalfHeight());
				default: return 0.f;
			}
		}
	}
	return 0.f;
}

AActor* UPushStatics::GetActorFromPushTarget(TScriptInterface<IPusherTarget> PushTarget)
{
	if (UObject* Object = PushTarget.GetObject())
	{
		// If the object is an actor, return it
		if (AActor* Actor = Cast<AActor>(Object))
		{
			return Actor;
		}
		// If the object is a component, return the owner
		else if (const UActorComponent* ActorComponent = Cast<UActorComponent>(Object))
		{
			return ActorComponent->GetOwner();
		}
		// Otherwise, unimplemented
		else
		{
			unimplemented();
		}
	}

	return nullptr;
}

void UPushStatics::GetPushTargetsFromActor(AActor* Actor, TArray<TScriptInterface<IPusherTarget>>& OutPushTargets)
{
	// If the actor is directly Pusher, return that.
	const TScriptInterface<IPusherTarget> PushActor(Actor);
	if (PushActor)
	{
		OutPushTargets.Add(PushActor);
	}

	// If the actor isn't Pusher, it might have a component that has a Push interface.
	TArray<UActorComponent*> PushComponents = Actor ? Actor->GetComponentsByInterface(UPusheeInstigator::StaticClass()) : TArray<UActorComponent*>();
	for (UActorComponent* PushComponent : PushComponents)
	{
		OutPushTargets.Add(TScriptInterface<IPusherTarget>(PushComponent));
	}
}

void UPushStatics::AppendPushTargetsFromOverlapResults(const TArray<FOverlapResult>& OverlapResults, TArray<TScriptInterface<IPusherTarget>>& OutPushTargets)
{
	// Iterate over all the overlap results and gather their push targets
	for (const FOverlapResult& Overlap : OverlapResults)
	{
		// If the actor is a Pusher, return that.
		TScriptInterface<IPusherTarget> PushActor(Overlap.GetActor());
		if (PushActor)
		{
			OutPushTargets.AddUnique(PushActor);
		}

		// If the actor isn't Pusher, it might have a component that has a Push interface.
		TScriptInterface<IPusherTarget> PushComponent(Overlap.GetComponent());
		if (PushComponent)
		{
			OutPushTargets.AddUnique(PushComponent);
		}
	}
}

void UPushStatics::AppendPushTargetsFromHitResult(const FHitResult& HitResult, TArray<TScriptInterface<IPusherTarget>>& OutPushTargets)
{
	// If the actor is a Pusher, return that.
	const TScriptInterface<IPusherTarget> PushActor(HitResult.GetActor());
	if (PushActor)
	{
		OutPushTargets.AddUnique(PushActor);
	}

	// If the actor isn't Pusher, it might have a component that has a Push interface.
	UPusherComponent* PusherComponent = HitResult.GetActor() ? HitResult.GetActor()->FindComponentByClass<UPusherComponent>() : nullptr;
	const TScriptInterface<IPusherTarget> PushComponent(PusherComponent);
	if (PushComponent)
	{
		OutPushTargets.AddUnique(PushComponent);
	}
}
