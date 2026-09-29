/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Motion/CubismMotion.h"

FCubismMotion::FCubismMotion(const UCubismMotion3Json* Json, const float InOffsetTime)
	: Duration(0.0f)
	, bLoop(false)
	, Fps(30.0f)
	, FadeInTime(0.0f)
	, FadeOutTime(0.0f)
	, CurveTable(nullptr)
	, StartTime(0.0f)
	, OffsetTime(InOffsetTime)
	, EndTime(-1.0f)
	, Weight(1.0f)
	, FadeOutSeconds(0.0f)
	, EndTimeSeconds(-1.0f)
	, bIsTriggeredFadeOut(false)
	, bFinished(false)
{
	if (!Json)
	{
		return;
	}

	Duration = Json->Duration;
	bLoop = Json->bLoop;
	Fps = Json->Fps;
	FadeInTime = Json->FadeInTime;
	FadeOutTime = Json->FadeOutTime;
	Curves = Json->Curves;
	CurveTable = Json->CurveTable;
	Events = Json->Events;
}

void FCubismMotion::Init(const float Time)
{
	State = (bLoop? ECubismMotionState::PlayInLoop : ECubismMotionState::Play);

	StartTime = Time;
	EndTime = Duration;
}

float FCubismMotion::UpdateFadeWeight(const TSharedPtr<FCubismMotion>& CubismMotion, float UserTimeSeconds)
{
	if (!CubismMotion.IsValid())
	{
		return 0.0f;
	}

	float FadeWeight = Weight;

	const float FadeIn = CubismMotion->FadeInTime <= 0.0f
		? 1.0f
		: EasingSin((UserTimeSeconds - CubismMotion->StartTime) / CubismMotion->FadeInTime);

	const float FadeOut = (CubismMotion->FadeOutTime <= 0.0f || CubismMotion->GetEndTime() < 0.0f)
		? 1.0f
		: EasingSin((CubismMotion->GetEndTime() - UserTimeSeconds) / CubismMotion->FadeOutTime);

	FadeWeight = FMath::Clamp(FadeWeight * FadeIn * FadeOut, 0.0f, 1.0f);

	return FadeWeight;
}

void FCubismMotion::SetWeight(float MotionWeight)
{
	this->Weight = MotionWeight;
}

float FCubismMotion::GetWeight() const
{
	return Weight;
}
void FCubismMotion::SetFadeout(float NewFadeOutSeconds)
{
	this->FadeOutSeconds = NewFadeOutSeconds;
	bIsTriggeredFadeOut = true;
}

void FCubismMotion::FadeOut(const float Time)
{
	// Fading out ends the loop; the motion finishes once the fade-out completes.
	State = ECubismMotionState::Play;

	StartFadeout(FadeOutTime, Time);
}

void FCubismMotion::StartFadeout(float NewFadeOutSeconds, float UserTimeSeconds)
{
	const float NewEndTimeSeconds = UserTimeSeconds + FMath::Max(NewFadeOutSeconds, 0.0f);
	bIsTriggeredFadeOut = true;

	if (EndTimeSeconds < 0.0f || NewEndTimeSeconds < EndTimeSeconds)
	{
		EndTimeSeconds = NewEndTimeSeconds;
	}
}

bool FCubismMotion::IsTriggeredFadeOut()
{
	return bIsTriggeredFadeOut;
}

float FCubismMotion::GetFadeOutSeconds()
{
	return FadeOutSeconds;
}

float FCubismMotion::GetEndTime()
{
	return EndTimeSeconds;
}

void FCubismMotion::IsFinished(bool F)
{
	bFinished = F;

	if (F)
	{
		State = ECubismMotionState::End;
	}
}

bool FCubismMotion::IsFinished() const
{
	return bFinished;
}

float FCubismMotion::GetValue(const FString Id, const float Time) const
{
	const UCurveTable* Table = CurveTable.Get();

	if (!Table)
	{
		return 0.0f;
	}

	const FRealCurve* Curve = Table->FindCurve(*Id, Id, false);

	if (!Curve)
	{
		return 0.0f;
	}

	return Curve->Eval(Time, 0.0f);
}
