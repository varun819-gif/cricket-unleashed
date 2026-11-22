// Blueprint / C++ access to our ball physics from Unreal (P1 "One Ball").
// Coordinates: the core works in metres with the origin at the striker's
// middle stump, +y towards the bowler, +x off side, +z up. In Unreal we use
// centimetres with X towards the bowler, Y off side, Z up:
//   Unreal (X, Y, Z) = 100 * (core y, core x, core z)
// UNTESTED: written without an Unreal Engine install (see docs/UNREAL_PROTOTYPE_SETUP.md).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CricketPhysicsLibrary.generated.h"

USTRUCT(BlueprintType)
struct FCricketBallPath
{
	GENERATED_BODY()

	// Ball centre every 10 ms from release, in Unreal centimetres.
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") TArray<FVector> Points;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") TArray<float> Times;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") bool bPitched = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") FVector BouncePoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") float BounceTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") float TimeAtBatter = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") bool bWouldHitStumps = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") float SwingCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Cricket") float TurnCm = 0.f;
};

UCLASS()
class CRICKETCORE_API UCricketPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// One delivery through the core physics (same code as the server).
	// LengthM: where it should pitch, metres from the striker's stumps.
	// LineM: + = outside off stump. Pitch: standard, green, dusty, bouncy, flat.
	UFUNCTION(BlueprintCallable, Category = "Cricket")
	static FCricketBallPath SimulateDelivery(float SpeedKph = 135.f, float LengthM = 6.f, float LineM = 0.1f,
	                                         float SeamAngleDeg = 0.f, float SpinRps = 0.f, float SpinTurn = 0.f,
	                                         const FString& Pitch = TEXT("standard"));

	// Reads one ball from a recording (<match>.replay.jsonl written by the
	// server or `cricket_sim --record`): its flight and its path after the shot.
	UFUNCTION(BlueprintCallable, Category = "Cricket")
	static bool LoadRecordedBall(const FString& ReplayFile, int32 BallIndex, FCricketBallPath& Flight,
	                             FCricketBallPath& AfterShot, FString& Summary);

	static FVector ToUnreal(double X, double Y, double Z) { return FVector(Y * 100.0, X * 100.0, Z * 100.0); }
};
