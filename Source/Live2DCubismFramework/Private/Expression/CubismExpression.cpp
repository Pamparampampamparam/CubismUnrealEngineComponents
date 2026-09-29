/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Expression/CubismExpression.h"

FCubismExpression::FCubismExpression(const UCubismExp3Json* Json)
	: FadeInTime(0.0f)
	, FadeOutTime(0.0f)
	, Weight(1.0f)
	, StartTime(0.0f)
	, EndTime(INFINITY)
{
	if (!Json)
	{
		return;
	}

	FadeInTime = Json->FadeInTime;
	FadeOutTime = Json->FadeOutTime;
	Parameters = Json->Parameters;
}

void FCubismExpression::Init(const float Time)
{
	State = ECubismExpressionState::Play;

	StartTime = Time;
	EndTime = INFINITY;
}

float FCubismExpression::CalcExpressionWeight(const float Time) const
{
	float CalcWeight = 1.0f;

	if (FadeInTime > 0.0f)
	{
		CalcWeight = EasingSin((Time - StartTime) / FadeInTime);
	}

	return FMath::Clamp(CalcWeight, 0.0f, 1.0f);
}

float FCubismExpression::UpdateWeight(const float Time)
{
	float FadeInWeight = 1.0f;
	float FadeOutWeight = 1.0f;
	float NewFadeWeight = Weight;

	if (FadeInTime > 0.0f)
	{
		FadeInWeight = EasingSin((Time - StartTime) / FadeInTime);
	}

	if (FadeOutTime > 0.0f && IsFadingOut())
	{
		FadeOutWeight = EasingSin((EndTime - Time) / FadeOutTime);
	}

	NewFadeWeight = FMath::Clamp(NewFadeWeight * FadeInWeight * FadeOutWeight, 0.0f, 1.0f);

	FadeWeight = NewFadeWeight;

	return NewFadeWeight;
}

void FCubismExpression::StartFadeout(const float Time)
{
	const float NewEndTime = FMath::Max(FadeOutTime, 0.0f) + Time;

	if (!IsFadingOut() || NewEndTime < EndTime)
	{
		EndTime = NewEndTime;
	}
}

bool FCubismExpression::IsFadingOut() const
{
	return FMath::IsFinite(EndTime);
}

bool FCubismExpression::IsFinished(const float Time) const
{
	if (State == ECubismExpressionState::End)
	{
		return true;
	}

	return IsFadingOut() && Time >= EndTime;
}
