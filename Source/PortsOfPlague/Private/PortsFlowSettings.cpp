// The Settings card (not on the website): graphics, audio and controls. Graphics are Unreal's own user
// settings; sound and camera settings are FPortsSettings. Every change takes effect at once and is saved.
#include "PortsGameFlow.h"

#include "PortsSettings.h"
#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/PlayerController.h"
#include "PortsMapActor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

using PortsUi::EButton;

namespace
{
	const TCHAR* const Section = TEXT("PortsOfPlague.Settings");
	const TCHAR* const KeySection = TEXT("PortsOfPlague.Keys");
}

FPortsSettings& FPortsSettings::Get()
{
	static FPortsSettings Settings;
	static bool bLoaded = false;
	if (!bLoaded) { bLoaded = true; Settings.Load(); }
	return Settings;
}

const TArray<FPortsSettings::FBinding>& FPortsSettings::Bindings()
{
	static const TArray<FBinding> All = {
		{ TEXT("ship"), TEXT("Ship Goods"), EKeys::One, 0 }, { TEXT("post"), TEXT("Open Trading Post"), EKeys::Two, 0 }, { TEXT("move"), TEXT("Move Family"), EKeys::Three, 0 },
		{ TEXT("prepare"), TEXT("Prepare Household"), EKeys::Four, 0 }, { TEXT("physician"), TEXT("Consult Physician"), EKeys::Five, 0 }, { TEXT("charity"), TEXT("Charity & Piety"), EKeys::Six, 0 },
		{ TEXT("marry"), TEXT("Arrange a Marriage"), EKeys::Seven, 0 }, { TEXT("land"), TEXT("Buy Abandoned Land"), EKeys::Eight, 0 }, { TEXT("loan"), TEXT("Take a Loan"), EKeys::Nine, 0 },
		{ TEXT("deal"), TEXT("Propose a Partnership"), EKeys::Zero, 0 }, { TEXT("gates"), TEXT("Close Your Gates"), EKeys::G, 0 }, { TEXT("end"), TEXT("End turn"), EKeys::E, 0 },
		{ TEXT("up"), TEXT("Move the map up"), EKeys::W, 1 }, { TEXT("down"), TEXT("Move the map down"), EKeys::S, 1 }, { TEXT("left"), TEXT("Move the map left"), EKeys::A, 1 },
		{ TEXT("right"), TEXT("Move the map right"), EKeys::D, 1 }, { TEXT("zoomIn"), TEXT("Zoom in"), EKeys::Equals, 1 }, { TEXT("zoomOut"), TEXT("Zoom out"), EKeys::Hyphen, 1 },
		{ TEXT("tiltUp"), TEXT("Tilt the camera up"), EKeys::PageUp, 1 }, { TEXT("tiltDown"), TEXT("Tilt the camera down"), EKeys::PageDown, 1 }, { TEXT("map"), TEXT("Whole map again"), EKeys::H, 1 },
		{ TEXT("rules"), TEXT("Rules"), EKeys::R, 1 }, { TEXT("journal"), TEXT("Historian's Journal"), EKeys::J, 1 }, { TEXT("sound"), TEXT("Sound effects on or off"), EKeys::M, 1 },
		{ TEXT("music"), TEXT("Music on or off"), EKeys::N, 1 },
	};
	return All;
}

FKey FPortsSettings::Key(FName Id) const
{
	if (const FKey* Found = Keys.Find(Id)) return *Found;
	for (const FBinding& B : Bindings()) if (B.Id == Id) return B.Default;
	return EKeys::Invalid;
}

FString FPortsSettings::LabelOf(const FKey& Key)
{
	// The zoom keys are known by what is printed on them.
	if (Key == EKeys::Equals) return TEXT("+");
	if (Key == EKeys::Hyphen) return TEXT("−");
	return Key.IsValid() ? Key.GetDisplayName(false).ToString() : FString(TEXT("none"));
}

FString FPortsSettings::KeyLabel(FName Id) const { return LabelOf(Key(Id)); }

bool FPortsSettings::CanBind(const FKey& Key)
{
	// "Any key" is Unreal's stand-in for whichever key is down, not a key.
	if (Key == EKeys::AnyKey) return false;
	if (!Key.IsValid() || Key.IsMouseButton() || Key.IsGamepadKey() || Key.IsTouch() || Key.IsModifierKey() || Key.IsAnalog()) return false;
	// Kept by the game: confirm and cancel, the arrows (which always move the map), Home, and the keys of the finale.
	static const FKey Kept[] = { EKeys::Enter, EKeys::Escape, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right, EKeys::Home, EKeys::SpaceBar, EKeys::Tab,
		EKeys::LeftCommand, EKeys::RightCommand, EKeys::CapsLock, EKeys::Add, EKeys::Subtract };
	for (const FKey& K : Kept) if (K == Key) return false;
	return true;
}

void FPortsSettings::SetKey(FName Id, const FKey& NewKey)
{
	const FKey Old = Key(Id);
	if (Old == NewKey) return;
	for (const FBinding& B : Bindings()) if (B.Id != Id && Key(B.Id) == NewKey) Keys.Add(B.Id, Old);
	Keys.Add(Id, NewKey);
	Save();
}

void FPortsSettings::ResetKeys()
{
	Keys.Reset();
	Save();
}

void FPortsSettings::Load()
{
	for (const FBinding& B : Bindings())
	{
		FString Name;
		if (!GConfig->GetString(KeySection, *B.Id.ToString(), Name, GGameUserSettingsIni)) continue;
		const FKey Saved(*Name);
		if (CanBind(Saved)) Keys.Add(B.Id, Saved);
	}
	// A saved file that somehow gives one key to two things falls back to the usual keys.
	TSet<FKey> Used;
	for (const FBinding& B : Bindings()) { bool bTwice = false; Used.Add(Key(B.Id), &bTwice); if (bTwice) { Keys.Reset(); break; } }

	GConfig->GetFloat(Section, TEXT("SoundVolume"), SoundVolume, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("MoveSpeed"), MoveSpeed, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("ZoomSpeed"), ZoomSpeed, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("InvertTilt"), bInvertTilt, GGameUserSettingsIni);
	SoundVolume = FMath::Clamp(SoundVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	MoveSpeed = FMath::Clamp(MoveSpeed, 0, 2);
	ZoomSpeed = FMath::Clamp(ZoomSpeed, 0, 2);
}

void FPortsSettings::Save() const
{
	GConfig->SetFloat(Section, TEXT("SoundVolume"), SoundVolume, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("MoveSpeed"), MoveSpeed, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("ZoomSpeed"), ZoomSpeed, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("InvertTilt"), bInvertTilt, GGameUserSettingsIni);
	// Only the keys the player changed are written.
	GConfig->EmptySection(KeySection, GGameUserSettingsIni);
	for (const TPair<FName, FKey>& Pair : Keys) GConfig->SetString(KeySection, *Pair.Key.ToString(), *Pair.Value.GetFName().ToString(), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

// What the open Settings card remembers: its tab, and the box its page is redrawn into.
struct FPortsSettingsView
{
	FString Tab = TEXT("graphics");
	TSharedPtr<SBox> Holder;
	TFunction<void(const FString&)> Close;
	float Width = 0.f;
	// The thing whose key is being chosen: the next key pressed becomes its key.
	FName Waiting;
	FString KeyNote;
};

void UPortsGameFlow::ShowSettings()
{
	const TSharedRef<FPortsSettingsView> View = MakeShared<FPortsSettingsView>();
	FPortsDialogOptions Opts;
	Opts.EnterValue = TEXT("close");
	View->Width = Opts.InnerWidth();
	SettingsView = View;
	Open([this, View](TFunction<void(const FString&)> Close)
	{
		View->Close = Close;
		View->Holder = SNew(SBox);
		View->Holder->SetContent(SettingsPage(View));
		return View->Holder.ToSharedRef();
	}, Opts, [this, View](const FString&)
	{
		// The page's buttons hold the view, and the view holds the page: let go.
		View->Holder.Reset();
		View->Close = nullptr;
		// The top bar and the side panel show the sound switches and the keys.
		if (bInGame) Refresh();
		else if (!SetupForm.IsValid() && !bTutorialLobby && !bResults) ShowMenu();
	});
}

TSharedRef<SWidget> UPortsGameFlow::SettingsPage(const TSharedRef<FPortsSettingsView>& View)
{
	const auto Redraw = [this, View]() { if (View->Holder.IsValid()) View->Holder->SetContent(SettingsPage(View)); };
	const auto Seg = [](const FString& Label, bool bOn, TFunction<void()> OnClick) { return PortsUi::Button(Label, OnClick, bOn ? EButton::SmallRed : EButton::Small); };
	FPortsSettings& Mine = FPortsSettings::Get();

	FPortsDoc Doc;
	Doc.H2(TEXT("Settings"));
	TArray<TSharedRef<SWidget>> Tabs;
	for (const TCHAR* Tab : { TEXT("graphics"), TEXT("audio"), TEXT("controls") })
	{
		const FString Id = Tab;
		Tabs.Add(Seg(Id == TEXT("graphics") ? TEXT("Graphics") : Id == TEXT("audio") ? TEXT("Audio") : TEXT("Controls"), View->Tab == Id, [View, Id, Redraw]() { View->Tab = Id; Redraw(); }));
	}
	Doc.Row(Tabs, 7);
	Doc.Space(6);

	if (View->Tab == TEXT("graphics"))
	{
		UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!G) Doc.P(TEXT("Graphics settings are not available here."));
		else
		{
			const auto Apply = [G, Redraw]() { G->ApplySettings(false); Redraw(); };
			// How the window really is now (a saved setting can be overruled when the game is started).
			const TSharedPtr<SWindow> Window = GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
			const bool bWindow = Window.IsValid() ? Window->GetWindowMode() == EWindowMode::Windowed : G->GetFullscreenMode() == EWindowMode::Windowed;
			Doc.Label(TEXT("Display"));
			Doc.Row({
				Seg(TEXT("Full screen"), !bWindow, [G, Apply]() { G->SetFullscreenMode(EWindowMode::WindowedFullscreen); G->SetScreenResolution(G->GetDesktopResolution()); Apply(); }),
				Seg(TEXT("Window"), bWindow, [G, Apply]() { G->SetFullscreenMode(EWindowMode::Windowed); G->SetScreenResolution(FIntPoint(1600, 900)); Apply(); }),
			}, 7);
			if (bWindow)
			{
				Doc.Label(TEXT("Window size"));
				TArray<TSharedRef<SWidget>> Sizes;
				const FIntPoint Now = Window.IsValid() ? Window->GetClientSizeInScreen().IntPoint() : G->GetScreenResolution(), Desk = G->GetDesktopResolution();
				for (const FIntPoint Size : { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440) })
				{
					// Only sizes this display can hold.
					if (Desk.X > 0 && (Size.X > Desk.X || Size.Y > Desk.Y)) continue;
					Sizes.Add(Seg(FString::Printf(TEXT("%d × %d"), Size.X, Size.Y), Now == Size, [G, Size, Apply]() { G->SetScreenResolution(Size); Apply(); }));
				}
				Doc.Row(Sizes, 7);
			}
			Doc.Label(TEXT("Quality"));
			TArray<TSharedRef<SWidget>> Levels;
			static const TCHAR* LevelNames[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Best") };
			// The level in force: Unreal's overall level, or, when only the picture's sharpness differs, the level everything else is at.
			int32 InForce = G->GetOverallScalabilityLevel();
			if (InForce < 0 && G->GetShadowQuality() == G->GetTextureQuality() && G->GetShadowQuality() == G->GetPostProcessingQuality()
				&& G->GetShadowQuality() == G->GetVisualEffectQuality() && G->GetShadowQuality() == G->GetViewDistanceQuality()) InForce = G->GetShadowQuality();
			for (int32 Level = 0; Level < 4; Level++) Levels.Add(Seg(LevelNames[Level], InForce == Level, [G, Level, Apply]() { G->SetOverallScalabilityLevel(Level); Apply(); }));
			Doc.Row(Levels, 7);
			Doc.Small(TEXT("Lower quality draws shadows, water and distant detail more simply, for a smoother picture on a slower computer."));
			Doc.Label(TEXT("Frame rate limit"));
			TArray<TSharedRef<SWidget>> Rates;
			const int32 Limit = FMath::RoundToInt32(G->GetFrameRateLimit());
			for (const int32 Rate : { 30, 60, 120, 0 }) Rates.Add(Seg(Rate ? FString::Printf(TEXT("%d"), Rate) : FString(TEXT("No limit")), Limit == Rate, [G, Rate, Apply]() { G->SetFrameRateLimit(Rate); Apply(); }));
			Doc.Row(Rates, 7);
			Doc.Label(TEXT("V-Sync"));
			Doc.Row({
				Seg(TEXT("On"), G->IsVSyncEnabled(), [G, Apply]() { G->SetVSyncEnabled(true); Apply(); }),
				Seg(TEXT("Off"), !G->IsVSyncEnabled(), [G, Apply]() { G->SetVSyncEnabled(false); Apply(); }),
			}, 7);
			Doc.Small(TEXT("On, the picture is drawn in step with the display, which stops it tearing."));
		}
	}
	else if (View->Tab == TEXT("audio"))
	{
		const auto Volumes = [&Seg, &Mine, Redraw](float FPortsSettings::* Which, TFunction<void()> Heard)
		{
			TArray<TSharedRef<SWidget>> Row;
			for (const int32 Percent : { 20, 40, 60, 80, 100 })
			{
				Row.Add(Seg(FString::Printf(TEXT("%d%%"), Percent), FMath::RoundToInt32(Mine.*Which * 100.f) == Percent, [Which, Percent, Redraw, Heard]()
				{
					FPortsSettings::Get().*Which = Percent / 100.f;
					FPortsSettings::Get().Save();
					if (Heard) Heard();
					Redraw();
				}));
			}
			return Row;
		};
		Doc.Label(TEXT("Sound effects"));
		Doc.Row({
			Seg(TEXT("On"), bSoundOn, [this, Redraw]() { SetSoundOn(true); Sound(TEXT("coin")); Redraw(); }),
			Seg(TEXT("Off"), !bSoundOn, [this, Redraw]() { SetSoundOn(false); Redraw(); }),
		}, 7);
		if (bSoundOn)
		{
			Doc.Label(TEXT("Sound effects volume"));
			Doc.Row(Volumes(&FPortsSettings::SoundVolume, [this]() { Sound(TEXT("coin")); }), 7);
		}
		Doc.Label(TEXT("Music"));
		Doc.Row({
			Seg(TEXT("On"), bMusicOn, [this, Redraw]() { SetMusicOn(true); Redraw(); }),
			Seg(TEXT("Off"), !bMusicOn, [this, Redraw]() { SetMusicOn(false); Redraw(); }),
		}, 7);
		if (bMusicOn)
		{
			Doc.Label(TEXT("Music volume"));
			Doc.Row(Volumes(&FPortsSettings::MusicVolume, nullptr), 7);
		}
		Doc.Small(TEXT("In a game, <key>{k:sound}</> switches the sound effects and <key>{k:music}</> the music."));
	}
	else
	{
		const auto Speeds = [&Seg, &Mine, Redraw](int32 FPortsSettings::* Which)
		{
			TArray<TSharedRef<SWidget>> Row;
			static const TCHAR* Names[] = { TEXT("Slow"), TEXT("Normal"), TEXT("Fast") };
			for (int32 Speed = 0; Speed < 3; Speed++) Row.Add(Seg(Names[Speed], Mine.*Which == Speed, [Which, Speed, Redraw]() { FPortsSettings::Get().*Which = Speed; FPortsSettings::Get().Save(); Redraw(); }));
			return Row;
		};
		FPortsDoc Left, Right;
		Left.Width(View->Width / 2 - 8);
		Right.Width(View->Width / 2 - 8);
		Left.Label(TEXT("Moving the map with the keys"));
		Left.Row(Speeds(&FPortsSettings::MoveSpeed), 7);
		Left.Label(TEXT("Zooming"));
		Left.Row(Speeds(&FPortsSettings::ZoomSpeed), 7);
		Right.Label(TEXT("Tilting with the mouse"));
		Right.Row({
			Seg(TEXT("Normal"), !Mine.bInvertTilt, [Redraw]() { FPortsSettings::Get().bInvertTilt = false; FPortsSettings::Get().Save(); Redraw(); }),
			Seg(TEXT("Inverted"), Mine.bInvertTilt, [Redraw]() { FPortsSettings::Get().bInvertTilt = true; FPortsSettings::Get().Save(); Redraw(); }),
		}, 7);
		Doc.Columns({ Left, Right }, 16);
		Doc.H3(TEXT("Keys"));
		Doc.Small(View->Waiting.IsNone() ? (View->KeyNote.IsEmpty() ? FString(TEXT("Click a key to change it, then press the new key. If that key already does something else, the two swap.")) : View->KeyNote)
			: FString(TEXT("<srisk>Press the new key now</>, or Esc to leave it as it is.")));
		FPortsDoc Columns[2];
		for (int32 c = 0; c < 2; c++)
		{
			Columns[c].Width(View->Width / 2 - 8);
			for (const FPortsSettings::FBinding& B : FPortsSettings::Bindings())
			{
				if (B.Group != c) continue;
				const FName Id = B.Id;
				const bool bWaiting = View->Waiting == Id;
				const FString Name = B.Name;
				const FString Label = bWaiting ? FString(TEXT("…")) : PortsUi::Esc(Mine.KeyLabel(Id));
				const float Wide = View->Width / 2 - 8;
				Columns[c].AddBuilt([Name, Label, bWaiting, Id, View, Redraw, Wide](float)
				{
					return SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ PortsUi::Rich(PortsUi::Esc(Name), TEXT("Ports.Body"), ETextJustify::Left, true, Wide - 120.f) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).MinDesiredWidth(96)[ PortsUi::Button(Label, [View, Id, Redraw]() { View->Waiting = View->Waiting == Id ? FName() : Id; View->KeyNote.Reset(); Redraw(); }, bWaiting ? EButton::SmallRed : EButton::Small) ]
						];
				}, FMargin(0, 2));
			}
		}
		Doc.Columns({ Columns[0], Columns[1] }, 16);
		Doc.Small(TEXT("Always the same: drag to move the map (or the arrow keys), the mouse wheel or two fingers on a trackpad to zoom, hold the right button and move up or down to tilt, <key>Enter</> to confirm and <key>Esc</> to cancel."));
		Doc.Row({ PortsUi::Button(TEXT("Put all keys back"), [View, Redraw]() { FPortsSettings::Get().ResetKeys(); View->Waiting = FName(); View->KeyNote = TEXT("All keys are back to the usual ones."); Redraw(); }, EButton::Small) }, 7);
	}
	Doc.Space(4);
	Doc.Buttons({ PortsUi::Button(TEXT("Close  <lsmall>Esc</>"), [View]() { if (View->Close) View->Close(TEXT("close")); }, EButton::Primary) });
	return Doc.Build(View->Width);
}

// While the Controls list is waiting for a key: the next keyboard key pressed is given to the chosen thing.
// Returns true while waiting, so that no key does anything else meanwhile.
bool UPortsGameFlow::SettingsKeyCapture()
{
	const TSharedPtr<FPortsSettingsView> View = SettingsView.Pin();
	if (!View.IsValid() || View->Waiting.IsNone() || !View->Holder.IsValid()) return false;
	const APlayerController* PC = Map && Map->GetWorld() ? Map->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return false;
	const auto Done = [this, View]() { View->Waiting = FName(); View->Holder->SetContent(SettingsPage(View.ToSharedRef())); };
	if (PC->WasInputKeyJustPressed(EKeys::Escape)) { Done(); return true; }
	TArray<FKey> All;
	EKeys::GetAllKeys(All);
	for (const FKey& Key : All)
	{
		if (Key == EKeys::AnyKey || Key.IsMouseButton() || Key.IsGamepadKey() || Key.IsTouch() || Key.IsAnalog() || Key.IsModifierKey() || !PC->WasInputKeyJustPressed(Key)) continue;
		if (!FPortsSettings::CanBind(Key)) View->KeyNote = FString::Printf(TEXT("<srisk>%s is kept by the game and cannot be chosen.</>"), *PortsUi::Esc(FPortsSettings::LabelOf(Key)));
		else
		{
			FPortsSettings::Get().SetKey(View->Waiting, Key);
			View->KeyNote.Reset();
		}
		Done();
		return true;
	}
	return true;
}
