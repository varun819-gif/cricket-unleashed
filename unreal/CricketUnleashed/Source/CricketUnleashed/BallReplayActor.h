// Moves a ball mesh along a path from the core physics (live or recorded).
// Drop it in a level, give it a sphere mesh (radius 3.6 cm), call PlayPath.
// UNTESTED: written without an Unreal Engine install.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CricketPhysicsLibrary.h"
#include "BallReplayActor.generated.h"

UCLASS()
class CRICKETUNLEASHED_API ABallReplayActor : public AActor
{
	GENERATED_BODY()

public:
	ABallReplayActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cricket") UStaticMeshComponent* Ball = nullptr;
	// Where the striker's middle stump is in the level (origin of the core coordinates).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cricket") FVector StrikerStumps = FVector::ZeroVector;
	// 1 = real time; 0.25 = slow-motion replay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cricket") float PlayRate = 1.f;

	UFUNCTION(BlueprintCallable, Category = "Cricket") void PlayPath(const FCricketBallPath& Path);
	UFUNCTION(BlueprintCallable, Category = "Cricket") bool IsPlaying() const { return bPlaying; }

	virtual void Tick(float DeltaSeconds) override;

private:
	FCricketBallPath Current;
	float Clock = 0.f;
	bool bPlaying = false;
};
