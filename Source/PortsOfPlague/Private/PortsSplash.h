// The splash screen shown when the game opens: the Unreal Engine logo exactly as Epic supplies it, laid out as
// Epic's guidelines for splash screens ask (brand.epicgames.com, "Splash screen logo"), with the trademark
// notice under it. The logo may not be recoloured, redrawn or animated: Epic turned down a customized version.
//
// Whether it is shown is set in Config/DefaultGame.ini ([PortsOfPlague.Splash] Style). See TRADEMARKS.txt.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SPortsSplash : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsSplash) : _Blank(false), _Still(false) {}
		// No logo and no notice: only the black screen the game opens on, which then fades away.
		SLATE_ARGUMENT(bool, Blank)
		// The black screen alone, staying as it is (the game's very first frames, before the splash begins).
		SLATE_ARGUMENT(bool, Still)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	// How long the splash is on screen, in seconds. The logo is whole from 0.7 seconds in and is held, still,
	// for 1.8 seconds before it goes.
	static float Seconds(bool bBlank = false) { return bBlank ? BlankSeconds : 3.1f + FadeSeconds; }
	// How long the black screen takes to fade away at the end, onto the start screen.
	static constexpr float FadeSeconds = 1.7f;
	static constexpr float BlankSeconds = 0.3f + FadeSeconds;
};
