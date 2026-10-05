// The splash screen shown when the game opens: the Unreal Engine logo, laid out as Epic's guidelines for
// splash screens ask (brand.epicgames.com, "Splash screen logo"), with the trademark notice under it.
//
// Which splash is shown, if any, is set in Config/DefaultGame.ini ([PortsOfPlague.Splash] Style). TRADEMARKS.txt
// says what has to be agreed with Epic before each may be shown to the public.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SPortsSplash : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsSplash) : _Custom(false), _Blank(false), _Still(false) {}
		// false: Epic's logo exactly as supplied, white on black. true: the same logo in the game's gold,
		// drawn in and gilded (a customization, which needs Epic's written approval).
		SLATE_ARGUMENT(bool, Custom)
		// No logo and no notice: only the dark screen the game opens on, which then fades away.
		SLATE_ARGUMENT(bool, Blank)
		// The dark screen alone, staying as it is (the game's very first frames, before the splash begins).
		SLATE_ARGUMENT(bool, Still)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	// How long the splash is on screen, in seconds.
	static float Seconds(bool bCustom, bool bBlank = false) { return bBlank ? BlankSeconds : (bCustom ? 4.7f : 3.1f) + FadeSeconds; }
	// How long the dark screen takes to fade away at the end, onto the start screen.
	static constexpr float FadeSeconds = 1.7f;
	static constexpr float BlankSeconds = 0.3f + FadeSeconds;
};
