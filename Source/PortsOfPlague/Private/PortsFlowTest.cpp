// For checking the game without a hand on it: a script of clicks and key presses, given on the command line,
// that goes through the same mouse and keyboard path a person's would.
//
//   -PortsPress="New Game|Everyone on their own device|+ Add a bot|see:Houses (1 of 6)|key:Enter|shot:/tmp/a.png|quit"
//
// Each step waits until it can be done (the button is on screen, can be clicked and is not covered), then the
// next one starts. A step that cannot be done in time is written to the log as FAILED and the script goes on.
//   Some label        click the button with this text ("Label#2" for the second such button)
//   key:Enter         press a key (Unreal's key names: Enter, Escape, One, E, SpaceBar…)
//   see:Some text     wait until this text is on screen
//   gone:Some text    wait until this text is no longer on screen
//   wait:3            wait this many seconds
//   scroll:-8         turn the mouse wheel eight notches down the page (a positive number: up)
//   shot:file.png     save a picture of the screen
//   quit              close the game
#include "PortsGameFlow.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Layout/WidgetPath.h"
#include "PortsMapActor.h"
#include "PortsUi.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Text as it reads on screen: markup tags gone, spaces tidied.
	FString Plain(const FString& Markup)
	{
		FString Out;
		bool bTag = false;
		for (const TCHAR C : Markup)
		{
			if (C == TEXT('<')) bTag = true;
			else if (C == TEXT('>') && bTag) bTag = false;
			else if (!bTag) Out.AppendChar(FChar::IsWhitespace(C) || C == 0x00A0 ? TEXT(' ') : C);
		}
		Out = Out.Replace(TEXT("&lt;"), TEXT("<")).Replace(TEXT("&gt;"), TEXT(">")).Replace(TEXT("&quot;"), TEXT("\"")).Replace(TEXT("&amp;"), TEXT("&"));
		while (Out.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
		return Out.TrimStartAndEnd();
	}

	void TextUnder(const TSharedRef<SWidget>& Widget, FString& Out)
	{
		if (!Widget->GetVisibility().IsVisible()) return;
		FString Own;
		if (Widget->GetType() == TEXT("SRichTextBlock")) Own = StaticCastSharedRef<SRichTextBlock>(Widget)->GetText().ToString();
		else if (Widget->GetType() == TEXT("STextBlock")) Own = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
		Own = Plain(Own);
		if (!Own.IsEmpty()) Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + Own;
		FChildren* Children = Widget->GetAllChildren();
		for (int32 i = 0; Children && i < Children->Num(); i++) TextUnder(Children->GetChildAt(i), Out);
	}

	struct FFound { TSharedRef<SWidget> Widget; FString Text; };
	void Buttons(const TSharedRef<SWidget>& Widget, TArray<FFound>& Out)
	{
		if (!Widget->GetVisibility().IsVisible()) return;
		if (Widget->GetType() == TEXT("SPortsButton"))
		{
			FString Text;
			TextUnder(Widget, Text);
			Out.Add({ Widget, Text });
		}
		FChildren* Children = Widget->GetAllChildren();
		for (int32 i = 0; Children && i < Children->Num(); i++) Buttons(Children->GetChildAt(i), Out);
	}

	TArray<TSharedRef<SWindow>> Windows()
	{
		TArray<TSharedRef<SWindow>> Out;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(Out);
		return Out;
	}

	FString ScreenText()
	{
		FString Out;
		for (const TSharedRef<SWindow>& Window : Windows()) TextUnder(Window, Out);
		return Out;
	}

	// Clicks the button with this label, as the mouse would. Why says what stopped it.
	bool Click(const FString& Step, FString& Why)
	{
		FString Label = Step;
		int32 Nth = 1;
		int32 Hash;
		if (Label.FindLastChar(TEXT('#'), Hash) && Label.Mid(Hash + 1).IsNumeric()) { Nth = FMath::Max(1, FCString::Atoi(*Label.Mid(Hash + 1))); Label.LeftInline(Hash); }
		FSlateApplication& App = FSlateApplication::Get();
		TArray<FFound> All;
		for (const TSharedRef<SWindow>& Window : Windows()) Buttons(Window, All);
		// The best match wins: the whole label, then its start, then anywhere in it.
		TArray<FFound> Matches;
		for (int32 Pass = 0; Pass < 3 && Matches.Num() == 0; Pass++)
		{
			for (const FFound& F : All)
			{
				if (Pass == 0 ? F.Text.Equals(Label, ESearchCase::CaseSensitive) : Pass == 1 ? F.Text.StartsWith(Label, ESearchCase::CaseSensitive) : F.Text.Contains(Label, ESearchCase::CaseSensitive)) Matches.Add(F);
			}
		}
		if (Matches.Num() < Nth) { Why = Matches.Num() ? TEXT("there are fewer such buttons") : TEXT("no such button on screen"); return false; }
		const FFound& Target = Matches[Nth - 1];
		const TSharedRef<SPortsButton> Button = StaticCastSharedRef<SPortsButton>(Target.Widget);
		if (!Button->CanClick()) { Why = TEXT("the button is greyed out"); return false; }
		const FGeometry& Geometry = Button->GetTickSpaceGeometry();
		const FVector2D Size = Geometry.GetAbsoluteSize();
		if (Size.X < 2 || Size.Y < 2) { Why = TEXT("the button has not been drawn yet"); return false; }
		const FVector2D Centre = FVector2D(Geometry.GetAbsolutePosition()) + Size * 0.5;
		// What the mouse would really hit there: a card or panel over the button stops the click.
		const FWidgetPath Path = App.LocateWindowUnderMouse(Centre, App.GetInteractiveTopLevelWindows());
		if (!Path.IsValid() || !Path.ContainsWidget(&Button.Get()))
		{
			Why = FString::Printf(TEXT("the button is covered or off screen; at %.0f,%.0f the mouse finds %s"), Centre.X, Centre.Y, Path.IsValid() ? *Path.GetLastWidget()->GetType().ToString() : TEXT("nothing"));
			// Further down or up the page: turn the mouse wheel towards it, as a person would, and look again.
			if (const TSharedPtr<SWindow> Window = App.FindWidgetWindow(Button))
			{
				const FSlateRect Rect = Window->GetRectInScreen();
				const FVector2D Middle = Rect.GetCenter();
				const float Turn = Centre.Y > Rect.Bottom - 30 ? -1.f : Centre.Y < Rect.Top + 30 ? 1.f : 0.f;
				if (Turn != 0)
				{
					App.ProcessMouseMoveEvent(FPointerEvent(0, Middle, Middle, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
					App.ProcessMouseWheelOrGestureEvent(FPointerEvent(0, Middle, Middle, TSet<FKey>(), EKeys::Invalid, Turn, FModifierKeysState()), nullptr);
				}
			}
			return false;
		}
		const TSharedPtr<FGenericWindow> Native = Path.GetWindow()->GetNativeWindow();
		const FModifierKeysState NoKeys;
		const TSet<FKey> Held({ EKeys::LeftMouseButton });
		App.ProcessMouseMoveEvent(FPointerEvent(0, Centre, Centre, TSet<FKey>(), EKeys::Invalid, 0, NoKeys));
		App.ProcessMouseButtonDownEvent(Native, FPointerEvent(0, Centre, Centre, Held, EKeys::LeftMouseButton, 0, NoKeys));
		App.ProcessMouseButtonUpEvent(FPointerEvent(0, Centre, Centre, TSet<FKey>(), EKeys::LeftMouseButton, 0, NoKeys));
		Why = FString::Printf(TEXT("“%s” at %.0f,%.0f"), *Target.Text.Left(60), Centre.X, Centre.Y);
		return true;
	}

	void Key(const FString& Name, bool bDown)
	{
		const FKey Pressed(*Name);
		const uint32* KeyCode = nullptr;
		const uint32* CharCode = nullptr;
		FInputKeyManager::Get().GetCodesFromKey(Pressed, KeyCode, CharCode);
		const FKeyEvent Event(Pressed, FModifierKeysState(), 0, false, CharCode ? *CharCode : 0, KeyCode ? *KeyCode : 0);
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(Event);
		else FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}
}

void UPortsGameFlow::TestScriptTick()
{
	const double Time = FPlatformTime::Seconds();
	if (TestStep < 0)
	{
		FString Script;
		TestStep = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("PortsPress="), Script, false)) Script.ParseIntoArray(TestSteps, TEXT("|"));
		float Patience = 40.f;
		FParse::Value(FCommandLine::Get(), TEXT("PortsPressWait="), Patience);
		TestStepPatience = Patience;
		TestStepAt = Time + 1.0;
		TestStepGiveUp = 0;
	}
	if (!TestKeyUp.IsEmpty()) { Key(TestKeyUp, false); TestKeyUp.Reset(); }
	if (!TestSteps.IsValidIndex(TestStep) || Time < TestStepAt) return;
	if (TestStepGiveUp <= 0) TestStepGiveUp = Time + TestStepPatience;
	const FString Step = TestSteps[TestStep].TrimStartAndEnd();
	FString Said;
	bool bDone = false;
	if (Step.StartsWith(TEXT("scroll:")))
	{
		// Turns the mouse wheel in the middle of the game's window: scroll:-8 is eight notches down the page.
		FSlateApplication& App = FSlateApplication::Get();
		const TArray<TSharedRef<SWindow>> Open = Windows();
		if (Open.Num())
		{
			const FVector2D Middle = Open[0]->GetRectInScreen().GetCenter();
			const int32 Notches = FCString::Atoi(*Step.Mid(7));
			App.ProcessMouseMoveEvent(FPointerEvent(0, Middle, Middle, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
			for (int32 i = 0; i < FMath::Abs(Notches); i++) App.ProcessMouseWheelOrGestureEvent(FPointerEvent(0, Middle, Middle, TSet<FKey>(), EKeys::Invalid, Notches < 0 ? -1.f : 1.f, FModifierKeysState()), nullptr);
		}
		UE_LOG(LogTemp, Display, TEXT("PortsPress: %d ok: %s"), TestStep + 1, *Step);
		TestStep++;
		TestStepGiveUp = 0;
		TestStepAt = Time + 0.7;
		return;
	}
	if (Step.StartsWith(TEXT("wait:"))) { TestStepAt = Time + FCString::Atod(*Step.Mid(5)); TestStepGiveUp = 0; TestStep++; return; }
	if (Step.StartsWith(TEXT("key:"))) { Key(Step.Mid(4), true); TestKeyUp = Step.Mid(4); bDone = true; }
	else if (Step.StartsWith(TEXT("see:"))) { bDone = ScreenText().Contains(Step.Mid(4), ESearchCase::CaseSensitive); Said = TEXT("that text is not on screen"); }
	else if (Step.StartsWith(TEXT("gone:"))) { bDone = !ScreenText().Contains(Step.Mid(5), ESearchCase::CaseSensitive); Said = TEXT("that text is still on screen"); }
	else if (Step.StartsWith(TEXT("shot:"))) { FScreenshotRequest::RequestScreenshot(Step.Mid(5), true, false); bDone = true; }
	else if (Step == TEXT("quit")) { UKismetSystemLibrary::QuitGame(Map, nullptr, EQuitPreference::Quit, false); bDone = true; }
	else if (Step == TEXT("text")) { UE_LOG(LogTemp, Display, TEXT("PortsPress: on screen: %s"), *ScreenText()); bDone = true; }
	else bDone = Click(Step, Said);
	if (!bDone && Time < TestStepGiveUp) return;
	if (bDone) { UE_LOG(LogTemp, Display, TEXT("PortsPress: %d ok: %s %s"), TestStep + 1, *Step, Step.Contains(TEXT(":")) ? TEXT("") : *Said); }
	else { UE_LOG(LogTemp, Warning, TEXT("PortsPress: %d FAILED: %s (%s)"), TestStep + 1, *Step, *Said); }
	TestStep++;
	TestStepGiveUp = 0;
	TestStepAt = Time + 0.7;
}
