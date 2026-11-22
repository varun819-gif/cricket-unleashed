#include "CricketPhysicsLibrary.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "cricket/BallPhysics.h"

FCricketBallPath UCricketPhysicsLibrary::SimulateDelivery(float SpeedKph, float LengthM, float LineM, float SeamAngleDeg,
                                                          float SpinRps, float SpinTurn, const FString& Pitch)
{
	cricket::phys::DeliverySpec Spec;
	Spec.speedKph = SpeedKph;
	Spec.targetLength = LengthM;
	Spec.targetLine = LineM;
	Spec.seamAngle = SeamAngleDeg;
	Spec.spinRps = SpinRps;
	Spec.spinTurn = SpinTurn;
	const cricket::phys::Pitch P = cricket::phys::Pitch::ByName(TCHAR_TO_UTF8(*Pitch));
	const cricket::phys::DeliveryPath Path = cricket::phys::SimulateDelivery(Spec, P);

	FCricketBallPath Out;
	double Next = -1.0;
	for (const cricket::phys::Sample& S : Path.samples)
	{
		if (S.t + 1e-9 < Next) continue;
		Next = S.t + 0.010;
		Out.Points.Add(ToUnreal(S.p.x, S.p.y, S.p.z));
		Out.Times.Add(static_cast<float>(S.t));
		if (S.p.y < -0.5) break;  // past the stumps
	}
	Out.bPitched = Path.pitched;
	Out.BouncePoint = ToUnreal(Path.pitchPoint.x, Path.pitchPoint.y, Path.pitchPoint.z);
	Out.BounceTime = static_cast<float>(Path.pitchTime);
	Out.TimeAtBatter = static_cast<float>(Path.timeAtCrease);
	Out.bWouldHitStumps = Path.wouldHitStumps;
	Out.SwingCm = static_cast<float>(Path.swingBeforePitch * 100.0);
	Out.TurnCm = static_cast<float>(Path.deviationOffPitch * 100.0);
	return Out;
}

namespace
{
void ReadPoints(const TArray<TSharedPtr<FJsonValue>>* Arr, FCricketBallPath& Out)
{
	if (!Arr) return;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
		if (P.Num() < 4) continue;
		Out.Times.Add(static_cast<float>(P[0]->AsNumber()));
		Out.Points.Add(UCricketPhysicsLibrary::ToUnreal(P[1]->AsNumber(), P[2]->AsNumber(), P[3]->AsNumber()));
	}
}
}  // namespace

bool UCricketPhysicsLibrary::LoadRecordedBall(const FString& ReplayFile, int32 BallIndex, FCricketBallPath& Flight,
                                              FCricketBallPath& AfterShot, FString& Summary)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ReplayFile)) return false;
	for (const FString& Line : Lines)
	{
		TSharedPtr<FJsonObject> Obj;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Obj) || !Obj.IsValid()) continue;
		if (Obj->GetStringField(TEXT("type")) != TEXT("ball") || Obj->GetIntegerField(TEXT("i")) != BallIndex) continue;
		const TSharedPtr<FJsonObject>* Phys = nullptr;
		if (!Obj->TryGetObjectField(TEXT("physics"), Phys)) return false;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if ((*Phys)->TryGetArrayField(TEXT("flight"), Arr)) ReadPoints(Arr, Flight);
		if ((*Phys)->TryGetArrayField(TEXT("afterShot"), Arr)) ReadPoints(Arr, AfterShot);
		Flight.bPitched = (*Phys)->GetBoolField(TEXT("pitched"));
		Summary = (*Phys)->GetStringField(TEXT("summary"));
		return Flight.Points.Num() > 0;
	}
	return false;
}
