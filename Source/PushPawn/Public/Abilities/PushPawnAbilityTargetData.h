﻿// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "PushPawnAbilityTargetData.generated.h"


/**
 * Ability target data that holds a direction vector, used for push abilities to send the direction of the push
 */
USTRUCT(BlueprintType)
struct FPushPawnAbilityTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	FPushPawnAbilityTargetData()
		: Direction(FVector::ZeroVector)
		, Distance(0.f)
		, StrengthScalar(1.f)
	{}

	FPushPawnAbilityTargetData(const FVector& InDirection, float InDistance, float InStrength)
		: Direction(InDirection)
		, Distance(InDistance)
		, StrengthScalar(InStrength)
	{}

	/** Direction of the push */
	UPROPERTY(BlueprintReadOnly, Category=Character)
	FVector_NetQuantizeNormal Direction;

	/** How far the pusher and pushee are from each other */
	UPROPERTY(BlueprintReadOnly, Category=Character)
	float Distance;

	/** The scalar to apply to the push strength */
	UPROPERTY(BlueprintReadOnly, Category=Character)
	float StrengthScalar;
	
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Direction.NetSerialize(Ar, Map, bOutSuccess);
		Ar << Distance;
		Ar << StrengthScalar;
		return true;
	}
	
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}
};

template<>
struct TStructOpsTypeTraits<FPushPawnAbilityTargetData> : public TStructOpsTypeTraitsBase2<FPushPawnAbilityTargetData>
{
	enum
	{
		WithNetSerializer = true	// For now this is REQUIRED for FGameplayAbilityTargetDataHandle net serialization to work
	};
};