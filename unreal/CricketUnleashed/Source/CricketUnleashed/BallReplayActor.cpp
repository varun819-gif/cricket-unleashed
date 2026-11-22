#include "BallReplayActor.h"

#include "Components/StaticMeshComponent.h"

ABallReplayActor::ABallReplayActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	RootComponent = Ball;
}

void ABallReplayActor::PlayPath(const FCricketBallPath& Path)
{
	Current = Path;
	Clock = Path.Times.Num() ? Path.Times[0] : 0.f;
	bPlaying = Path.Points.Num() > 1;
	if (Path.Points.Num()) SetActorLocation(StrikerStumps + Path.Points[0]);
}

void ABallReplayActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPlaying) return;
	Clock += DeltaSeconds * PlayRate;
	const TArray<float>& T = Current.Times;
	int32 i = 0;
	while (i + 1 < T.Num() && T[i + 1] < Clock) ++i;
	if (i + 1 >= T.Num())
	{
		SetActorLocation(StrikerStumps + Current.Points.Last());
		bPlaying = false;
		return;
	}
	// Straight-line blend between samples (they are 10-50 ms apart).
	const float Span = FMath::Max(1e-4f, T[i + 1] - T[i]);
	const float A = FMath::Clamp((Clock - T[i]) / Span, 0.f, 1.f);
	SetActorLocation(StrikerStumps + FMath::Lerp(Current.Points[i], Current.Points[i + 1], A));
}
