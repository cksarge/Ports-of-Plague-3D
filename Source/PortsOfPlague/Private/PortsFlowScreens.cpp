// The screens: title menu, new-game setup, the game's top bar and side panel,
// the final scores, and the Rules, Journal, city and credits pages. Their
// wording and layout follow src/ui/menu.js, game.js, endgame.js and panels.js,
// and their look follows src/styles/game.css.
#include "PortsGameFlow.h"

#include "PortsCameraPawn.h"
#include "PortsMapActor.h"
#include "PortsNet.h"
#include "GameFramework/PlayerController.h"
#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"

using V = FPortsValue;
using PortsUi::Esc;
using PortsUi::Rich;
using PortsUi::EButton;
using PortsUi::Color;

// The setup screen's choices so far.
struct FPortsSetupForm
{
	int32 count = 2;
	FString difficulty = TEXT("chronicler");
	FString mode = TEXT("standard");
	bool hints = true;
	bool prePlague = true;
	bool timer = true;
	TArray<FPortsSetupPlayer> players;
	FString error;
	// Everyone on their own device: this screen opens a room and becomes the big screen.
	bool bDevices = false;
	FString roomError;
};

namespace
{
	// The camera's whole-map view allows for the side panel only while a game is on screen.
	void SetSidePanel(const APortsMapActor* Map, bool bShowing)
	{
		APlayerController* PC = Map && Map->GetWorld() ? Map->GetWorld()->GetFirstPlayerController() : nullptr;
		if (APortsCameraPawn* Pawn = PC ? Cast<APortsCameraPawn>(PC->GetPawn()) : nullptr) Pawn->SetSidePanel(bShowing);
	}

	const FPortsData& Data() { return FPortsData::Get(); }
	int32 Cfg(const TCHAR* Path) { return FPortsData::Get().Int(Path); }

	FString CityName(const FString& Id)
	{
		const FPortsCity* City = Data().FindCity(Id);
		return City ? City->Name : Id;
	}

	// A whole screen: one illuminated frame in the middle, over a backdrop picture.
	TSharedRef<SWidget> Page(const TSharedRef<SWidget>& Content, float Width, const TCHAR* Backdrop, bool bCoverMap, const TSharedPtr<SPortsRoot>& Root, bool bMiddle = false)
	{
		const TSharedRef<SWidget> Frame = SNew(SBox).WidthOverride(Width)[ PortsUi::Frame(Content) ];
		const TSharedRef<SWidget> Back = SNew(SImage).Image(PortsUi::PictureBrush(Backdrop)).Visibility(bCoverMap ? EVisibility::Visible : EVisibility::HitTestInvisible);
		if (bCoverMap) Root->MarkSolid(Back);
		Root->MarkSolid(Frame);
		// The scroll bar lies over the right edge instead of taking room, so the page does not shift sideways when it appears.
		const TSharedRef<SScrollBar> Bar = SNew(SScrollBar);
		return SNew(SOverlay)
			+ SOverlay::Slot()[ Back ]
			+ SOverlay::Slot().VAlign(bMiddle ? VAlign_Center : VAlign_Fill)
			[
				SNew(SScrollBox).ExternalScrollbar(Bar)
				+ SScrollBox::Slot().HAlign(HAlign_Center).Padding(FMargin(32))[ Frame ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right)[ Bar ];
	}

	FString ConfigText(const FString& Path)
	{
		const V* Node = &Data().Config();
		TArray<FString> Keys;
		Path.ParseIntoArray(Keys, TEXT("."));
		for (const FString& Key : Keys) Node = &Node->Get(Key);
		if (Node->IsString()) return Node->AsString();
		if (Node->IsBool()) return Node->AsBool() ? TEXT("true") : TEXT("false");
		if (Node->IsNumber())
		{
			const double N = Node->AsNumber();
			return N == FMath::FloorToDouble(N) ? FString::Printf(TEXT("%lld"), static_cast<int64>(N)) : FString::SanitizeFloat(N);
		}
		return FString::Printf(TEXT("{{%s}}"), *Path);
	}

	// Rule text: fills {{config.path}} and {{fact:ID}}, then **bold** and *italic* (render/template.js).
	FString RuleText(const FString& Text)
	{
		FString Filled;
		for (int32 i = 0; i < Text.Len();)
		{
			const int32 Open = Text.Find(TEXT("{{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
			const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("}}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Open == INDEX_NONE || Close == INDEX_NONE) { Filled += Text.Mid(i); break; }
			Filled += Text.Mid(i, Open - i);
			const FString Key = Text.Mid(Open + 2, Close - Open - 2).TrimStartAndEnd();
			Filled += Key.StartsWith(TEXT("fact:")) ? FString::Printf(TEXT("\x01%s\x02"), *Key.Mid(5)) : ConfigText(Key);
			i = Close + 2;
		}
		FString Out = Esc(Filled);
		const auto Wrap = [&Out](const TCHAR* Mark, const TCHAR* Tag)
		{
			const int32 Len = FCString::Strlen(Mark);
			for (;;)
			{
				const int32 A = Out.Find(Mark, ESearchCase::CaseSensitive);
				const int32 B = A == INDEX_NONE || A + Len + 1 >= Out.Len() ? INDEX_NONE : Out.Find(Mark, ESearchCase::CaseSensitive, ESearchDir::FromStart, A + Len + 1);
				if (A == INDEX_NONE || B == INDEX_NONE) break;
				Out = Out.Left(A) + FString::Printf(TEXT("<%s>"), Tag) + Out.Mid(A + Len, B - A - Len) + TEXT("</>") + Out.Mid(B + Len);
			}
		};
		Wrap(TEXT("**"), TEXT("b"));
		Wrap(TEXT("*"), TEXT("i"));
		return Out.Replace(TEXT("\x01"), TEXT("<id>[")).Replace(TEXT("\x02"), TEXT("]</>"));
	}

	TSharedRef<SWidget> Wide(const TSharedRef<SWidget>& Button, float Width)
	{
		return SNew(SBox).WidthOverride(Width)[ Button ];
	}
}

// ---------- The frame round the map ----------

// The website sets the map into the walnut table inside four rings: dark, gold, bronze, dark.
class SPortsMapFrame : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPortsMapFrame) {}
	SLATE_END_ARGS()

	// How far the map is set in from the edges (.map-wrap: 0.9rem), and how round its corners are.
	static constexpr float Band = 15.f;
	static constexpr float Corner = 8.f;

	void Construct(const FArguments&)
	{
		Wood = *PortsUi::PictureBrush(TEXT("bg_wood"));
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 16); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FVector2f Size = G.GetLocalSize();
		if (Size.X < Band * 3 || Size.Y < Band * 3) return Layer;
		const auto At = [&G](const FVector2f& Pos, const FVector2f& Extent) { return G.ToPaintGeometry(Extent, FSlateLayoutTransform(Pos)); };

		// The dark that gathers at the map's edges (inset 0 0 50px rgba(0,0,0,.45)).
		const FLinearColor Dark(0, 0, 0, 0.30f), Clear(0, 0, 0, 0);
		const float Fade = 46.f;
		const FVector2f Inner(Size.X - Band * 2, Size.Y - Band * 2);
		const auto Shade = [&](const FVector2f& Pos, const FVector2f& Extent, EOrientation Way, bool bDarkFirst)
		{
			TArray<FSlateGradientStop> Stops;
			const FVector2f End = Way == Orient_Vertical ? FVector2f(Extent.X, 0) : FVector2f(0, Extent.Y);
			Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, bDarkFirst ? Dark : Clear));
			Stops.Add(FSlateGradientStop(FVector2D(End), bDarkFirst ? Clear : Dark));
			FSlateDrawElement::MakeGradient(Out, Layer, At(Pos, Extent), Stops, Way, ESlateDrawEffect::None);
		};
		Shade(FVector2f(Band, Band), FVector2f(Inner.X, Fade), Orient_Horizontal, true);
		Shade(FVector2f(Band, Size.Y - Band - Fade), FVector2f(Inner.X, Fade), Orient_Horizontal, false);
		Shade(FVector2f(Band, Band), FVector2f(Fade, Inner.Y), Orient_Vertical, true);
		Shade(FVector2f(Size.X - Band - Fade, Band), FVector2f(Fade, Inner.Y), Orient_Vertical, false);

		// The table: four strips of the same walnut picture the side panel lies on.
		const auto Strip = [&](const FVector2f& Pos, const FVector2f& Extent)
		{
			FSlateBrush Piece = Wood;
			Piece.SetUVRegion(FBox2f(FVector2f(Pos.X / Size.X, Pos.Y / Size.Y), FVector2f((Pos.X + Extent.X) / Size.X, (Pos.Y + Extent.Y) / Size.Y)));
			FSlateDrawElement::MakeBox(Out, Layer + 1, At(Pos, Extent), &Piece, ESlateDrawEffect::None, FLinearColor::White);
		};
		Strip(FVector2f(0, 0), FVector2f(Size.X, Band));
		Strip(FVector2f(0, Size.Y - Band), FVector2f(Size.X, Band));
		Strip(FVector2f(0, Band), FVector2f(Band, Inner.Y));
		Strip(FVector2f(Size.X - Band, Band), FVector2f(Band, Inner.Y));

		// The rings, from the outside in. A dark one the full width lies under them so no map shows between.
		const auto Ring = [&](float Inset, float Width, const TCHAR* Hex)
		{
			const float Radius = Corner + (Band - Inset);
			const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, Radius, PortsUi::Color(Hex), Width);
			FSlateDrawElement::MakeBox(Out, Layer + 2, At(FVector2f(Inset, Inset), FVector2f(Size.X - Inset * 2, Size.Y - Inset * 2)), &Brush, ESlateDrawEffect::None, FLinearColor::Transparent);
		};
		Ring(Band - 9.f, 9.5f, TEXT("#3b2413"));
		Ring(Band - 7.f, 2.2f, TEXT("#7a560c"));
		Ring(Band - 5.f, 3.2f, TEXT("#d9a82b"));
		return Layer + 2;
	}

private:
	FSlateBrush Wood;
};

namespace
{
	// The side panel (.sidebar): how wide it is, and how much of that is margin.
	const float SideWidth = 416.f;
	const float SidePad = 13.f;
	// Checking the game: start with the map legend open.
	bool bTestLegend = false;
}

// ---------- Menu ----------

void UPortsGameFlow::ShowMenu()
{
	bInGame = false;
	bResults = false;
	bPlagueCard = false;
	CloseLobbyRoom();
	if (Room.IsValid()) { Room->Close(); Room.Reset(); }
	Ui.roomCode.Reset();
	if (Map) Map->ClearState();
	SetSidePanel(Map, false);
	FPortsState Saved;
	FPortsUiState SavedUi;
	const bool bSaved = FPortsData::EnsureLoaded() && LoadSaved(Saved, SavedUi);

	FPortsDoc Doc;
	Doc.Add(PortsUi::ShimmerTitle(FPortsData::Get().Config().Get(TEXT("title")).AsString(), 92), FMargin(0, 0, 0, 0));
	Doc.Text(TEXT("Trade, survival and conscience in the years of the Black Death, 1347–1353"), TEXT("Ports.Subtitle"), ETextJustify::Center, FMargin(0, 4, 0, 20));
	Doc.DropCap(TEXT("In 1347 Italian merchant ships carried a deadly plague from the Black Sea into the ports of Europe. You lead a merchant house in one of the great trading cities. Grow rich from trade, but every ship may carry the plague. Protect your family, keep your good name, and face the same hard choices people faced six and a half centuries ago."));
	Doc.Space(10);
	FPortsDoc Buttons;
	if (bSaved)
	{
		TArray<FString> Names;
		for (const FPortsPlayer& P : Saved.players) Names.Add(Esc(P.name));
		const FPortsRoundInfo Info = Ports::RoundInfo(Saved);
		Buttons.Add(PortsUi::Button(FString::Printf(TEXT("Continue saved game\n<btnsmalllight>%s · %s</>"), Info.bValid ? *Esc(Info.label) : TEXT("Start"), *FString::Join(Names, TEXT(", "))), [this]() { ContinueSaved(); }, EButton::Primary, true, FString(), 380 - 48), FMargin(0, 6));
	}
	Buttons.Add(PortsUi::Button(TEXT("New game"), [this]() { SetupForm.Reset(); ShowSetup(); }, bSaved ? EButton::Normal : EButton::Primary), FMargin(0, 6));
	Buttons.Add(PortsUi::Button(TEXT("Rules  <key>R</>"), [this]() { ShowRules(); }), FMargin(0, 6));
	Buttons.Add(PortsUi::Button(TEXT("About & credits"), [this]() { ShowCredits(); }), FMargin(0, 6));
	Buttons.AddBuilt([this](float)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[ SNew(SSpacer) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)[ PortsUi::Button(bSoundOn ? TEXT("Sound on") : TEXT("Sound off"), [this]() { SetSoundOn(!bSoundOn); ShowMenu(); }, EButton::Small) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0)[ PortsUi::Button(bMusicOn ? TEXT("Music on") : TEXT("Music off"), [this]() { SetMusicOn(!bMusicOn); ShowMenu(); }, EButton::Small) ]
			+ SHorizontalBox::Slot().FillWidth(1)[ SNew(SSpacer) ];
	}, FMargin(0, 6));
	Doc.Add(SNew(SBox).HAlign(HAlign_Center)[ Wide(Buttons.Build(380), 380) ]);
	// .menu-foot: who can play, and for how long (the website's line; this version is played with a mouse or keyboard).
	if (FPortsData::Get().IsLoaded())
	{
		const auto ToFive = [](int32 Minutes) { return FMath::RoundToInt32(Minutes / 5.0) * 5; };
		Doc.Text(FString::Printf(TEXT("1–%d players (bots can play any house) on one device or each on their own · about %d–%d minutes · mouse or keyboard"),
			Cfg(TEXT("players.max")), ToFive(Cfg(TEXT("timeEstimates.quick.2"))), ToFive(Cfg(TEXT("timeEstimates.standard.6")))), TEXT("Ports.Small"), ETextJustify::Center, FMargin(0, 16, 0, 0));
	}
	Doc.Space(8);
	const TSharedRef<SWidget> Built = Doc.Build(740 - 58);
	Root->SetScreen(Page(Built, 740, TEXT("bg_title_veil"), false, Root, true));
}

// ---------- New game ----------

// Opens a room for the new-game screen's lobby; the screen redraws as players join and leave.
void UPortsGameFlow::OpenLobbyRoom()
{
	if (LobbyRoom.IsValid() || !SetupForm.IsValid()) return;
	if (!FPortsTransport::IsAvailable())
	{
		SetupForm->roomError = TEXT("Multi-device play is not set up on this copy of the game yet.");
		return;
	}
	LobbyRoom = MakeShared<FPortsRoom>();
	TWeakObjectPtr<UPortsGameFlow> Weak(this);
	LobbyRoom->OnChange = [Weak]() { if (Weak.IsValid() && Weak->SetupForm.IsValid() && Weak->SetupForm->bDevices && Weak->SetupHolder.IsValid()) Weak->ShowSetup(); };
	// For checking the game: -PortsRoomCode=XXXX asks for one particular code.
	FString AskFor;
	FParse::Value(FCommandLine::Get(), TEXT("PortsRoomCode="), AskFor);
	LobbyRoom->Open(AskFor, TArray<FPortsSeat>(), false, [Weak](bool bOk)
	{
		if (!Weak.IsValid() || !Weak->SetupForm.IsValid()) return;
		if (!bOk)
		{
			Weak->SetupForm->roomError = TEXT("Could not open a room. Check the internet connection and try again.");
			Weak->LobbyRoom.Reset();
		}
		if (Weak->SetupForm->bDevices && Weak->SetupHolder.IsValid()) Weak->ShowSetup();
	});
}

// For checking multi-device play without a hand on this screen: as soon as enough devices have joined the lobby,
// a bot is added and a Quick game without pre-plague rounds starts. The spec can ask otherwise:
// "lobbyplay4" waits for four devices, and "nobot", "standard", "mortality", "pre" and "timer" change the game.
void UPortsGameFlow::TestLobbyPlay()
{
	if (!bTestLobbyPlay || !LobbyRoom.IsValid() || !LobbyRoom->bReady || LobbyRoom->Seats.Num() < TestLobbyHumans) return;
	bTestLobbyPlay = false;
	if (!TestLobbySpec.Contains(TEXT("nobot")) && LobbyRoom->Seats.Num() < 6)
	{
		FString Home;
		for (const FString& H : FPortsData::Get().HomeCities) if (!LobbyRoom->Seats.ContainsByPredicate([&H](const FPortsSeat& S) { return S.home == H; })) { Home = H; break; }
		LobbyRoom->AddBot(TEXT("House of the Lion"), Home, TEXT("medium"));
	}
	FPortsSetup Setup;
	for (int32 i = 0; i < LobbyRoom->Seats.Num(); i++)
	{
		const FPortsSeat& S = LobbyRoom->Seats[i];
		FPortsSetupPlayer P;
		P.name = S.name; P.home = S.home; P.bot = S.bot; P.skill = S.skill.IsEmpty() ? FString(TEXT("medium")) : S.skill;
		P.color = Ports::PLAYER_STYLES[i].color; P.colorName = Ports::PLAYER_STYLES[i].colorName; P.crest = Ports::PLAYER_STYLES[i].crest;
		Setup.players.Add(P);
	}
	Setup.mode = TestLobbySpec.Contains(TEXT("standard")) ? TEXT("standard") : TEXT("quick");
	Setup.difficulty = TestLobbySpec.Contains(TEXT("mortality")) ? TEXT("mortality") : TEXT("chronicler");
	Setup.prePlague = TestLobbySpec.Contains(TEXT("pre"));
	Setup.timer = bTestLobbyTimer;
	Setup.seed = TEXT("test-") + TestLobbySpec;
	Room = LobbyRoom;
	LobbyRoom.Reset();
	Room->bStarted = true;
	AdoptRoom();
	SetupForm->bDevices = false;
	BeginGame(Setup, false);
}

void UPortsGameFlow::CloseLobbyRoom()
{
	if (LobbyRoom.IsValid()) LobbyRoom->Close();
	LobbyRoom.Reset();
}

void UPortsGameFlow::ShowSetup()
{
	static const TCHAR* DefaultNames[] = { TEXT("House of the Anchor"), TEXT("House of the Lion"), TEXT("House of the Rose"), TEXT("House of the Star"), TEXT("House of the Ship"), TEXT("House of the Sun") };
	static const TCHAR* DefaultHomes[] = { TEXT("genoa"), TEXT("bruges"), TEXT("venice"), TEXT("london"), TEXT("florence"), TEXT("lubeck") };
	if (!SetupForm.IsValid())
	{
		SetupForm = MakeShared<FPortsSetupForm>();
		for (int32 i = 0; i < 6; i++)
		{
			FPortsSetupPlayer P;
			P.name = DefaultNames[i];
			P.home = DefaultHomes[i];
			P.skill = TEXT("medium");
			P.color = Ports::PLAYER_STYLES[i].color;
			P.colorName = Ports::PLAYER_STYLES[i].colorName;
			P.crest = Ports::PLAYER_STYLES[i].crest;
			SetupForm->players.Add(P);
		}
	}
	const TSharedPtr<FPortsSetupForm> Form = SetupForm;
	const auto Redraw = [this]() { ShowSetup(); };
	const auto Seg = [](const FString& Label, bool bOn, TFunction<void()> OnClick) { return PortsUi::Button(Label, OnClick, bOn ? EButton::SmallOn : EButton::Small); };
	const V& Config = Data().Config();
	const FPortsBoxLook InputLook = PortsUi::PlainLook(Color(TEXT("#fffdf6")), 8, Color(TEXT("#4d3a22")), 2);
	const float InnerWidth = 1180 - 58;

	FPortsDoc Doc;
	Doc.Width(InnerWidth);
	Doc.H1(TEXT("New Game"));
	// A finished game's room is closed once a new game is being set up.
	if (Room.IsValid() && !bInGame) { Room->Close(); Room.Reset(); }
	const bool bDevices = Form->bDevices;
	Doc.Label(TEXT("Play on"));
	Doc.Row({ Seg(TEXT("This device only"), !bDevices, [this, Form, Redraw]() { Form->bDevices = false; CloseLobbyRoom(); Redraw(); }),
		Seg(TEXT("Everyone on their own device"), bDevices, [this, Form, Redraw]() { Form->bDevices = true; Form->roomError.Reset(); OpenLobbyRoom(); Redraw(); }) }, 7);
	Doc.Small(bDevices ? TEXT("This screen shows the map for everyone. Each player joins on a phone, tablet or computer with the room code and takes their turn there.") : TEXT("Players take turns on this device and pass it on."));
	Doc.Space(6);
	const TArray<FPortsSeat> NoSeats;
	const TArray<FPortsSeat>& LobbySeats = LobbyRoom.IsValid() ? LobbyRoom->Seats : NoSeats;
	if (bDevices)
	{
		// What players waiting in the lobby are told about the game.
		if (LobbyRoom.IsValid())
		{
			const V Options = V::Object({ { TEXT("mode"), V(Form->mode) }, { TEXT("difficulty"), V(Form->difficulty) }, { TEXT("prePlague"), Form->prePlague }, { TEXT("timer"), Form->timer } });
			if (Options.ToJson() != LobbyRoom->Options.ToJson()) { LobbyRoom->Options = Options; LobbyRoom->PushLobby(); }
		}
		if (!Form->roomError.IsEmpty())
		{
			Doc.P(FString::Printf(TEXT("<risk>%s</>"), *Esc(Form->roomError)));
			if (FPortsTransport::IsAvailable()) Doc.Row({ PortsUi::Button(TEXT("Try again"), [this, Form, Redraw]() { Form->roomError.Reset(); OpenLobbyRoom(); Redraw(); }, EButton::Small) }, 7);
		}
		else if (!LobbyRoom.IsValid() || !LobbyRoom->bReady) Doc.P(TEXT("Opening a room…"));
		else
		{
			// .room-code-box: where to join, and the code, large enough to read across a room.
			FPortsBoxLook CodeBox;
			CodeBox.Top = Color(TEXT("#8f1a12")); CodeBox.Bottom = Color(TEXT("#5c0d09"));
			CodeBox.Radius = 12;
			CodeBox.Border = Color(TEXT("#d9a82b")); CodeBox.BorderWidth = 3;
			Doc.Add(PortsUi::Box(CodeBox, SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<roomat>Join at </><roomaddr>%s</><roomat> → </><roomem>Join a game</>"), *Esc(FPortsTransport::JoinAddress())), TEXT("Ports.Body"), ETextJustify::Left, false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<roomcode>%s</>"), *Esc(LobbyRoom->Code)), TEXT("Ports.Body"), ETextJustify::Right, false) ], FMargin(18, 10)), FMargin(0, 6));
			Doc.H3(FString::Printf(TEXT("Houses (%d of %d)"), LobbySeats.Num(), Cfg(TEXT("players.max"))));
			if (LobbySeats.Num() == 0) Doc.Small(TEXT("Waiting for players… Each player opens the game on their own device, chooses <i>Join a game</i> and types the code."));
			const int32 SeatColumns = FMath::Clamp(LobbySeats.Num(), 1, 3);
			const float SeatWidth = (InnerWidth - 16.f * (SeatColumns - 1)) / SeatColumns;
			TArray<TSharedRef<SWidget>> SeatCards;
			const V& AllSkills = Config.Get(TEXT("bots")).Get(TEXT("skills"));
			for (int32 i = 0; i < LobbySeats.Num(); i++)
			{
				const FPortsSeat& S = LobbySeats[i];
				const FPortsPlayerStyle& Style = Ports::PLAYER_STYLES[i];
				const FLinearColor House = Color(Style.color);
				FPortsDoc Card;
				Card.Width(SeatWidth - 34);
				// The house's crest and name, and whether its device is connected (.link-dot): green when it is.
				Card.Add(SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)[ PortsUi::Crest(Style.color, Style.crest, 26) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<capsb>%s</>%s"), *Esc(S.name), S.bot ? TEXT("  <small>Bot</>") : TEXT("")), TEXT("Ports.Body"), ETextJustify::Left, false) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[
						S.bot ? StaticCastSharedRef<SWidget>(SNew(SSpacer)) : StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(11).HeightOverride(11)[ PortsUi::Box(PortsUi::PlainLook(Color(S.online ? TEXT("#2e9d5b") : TEXT("#b9ad92")), 5.5f, FLinearColor(0, 0, 0, 0.3f), 1), SNew(SSpacer)) ])
					]);
				Card.Small(FString::Printf(TEXT("%s · %s"), *Esc(CityName(S.home)), *Esc(Style.colorName)));
				if (S.bot)
				{
					Card.Label(TEXT("Bot skill"));
					TArray<TSharedRef<SWidget>> Skills;
					for (int32 k = 0; k < AllSkills.GetKeys().Num(); k++)
					{
						const FString Key = AllSkills.GetKeys()[k];
						Skills.Add(Seg(Esc(AllSkills.ValueAt(k).Get(TEXT("label")).AsString()), S.skill == Key, [this, i, Key]() { if (LobbyRoom.IsValid()) LobbyRoom->SetSkill(i, Key); }));
					}
					Card.Row(Skills, 7);
					Card.Small(Esc(AllSkills.Get(S.skill).Get(TEXT("description")).AsString()));
				}
				Card.Row({ PortsUi::Button(TEXT("Remove"), [this, i]() { if (LobbyRoom.IsValid()) LobbyRoom->RemoveSeat(i); }, EButton::Ghost) }, 7);
				FPortsBoxLook CardLook = PortsUi::PlainLook(Color(TEXT("#fffaf0")), 12, House, 3);
				CardLook.TopBar = House;
				CardLook.TopBarHeight = 9;
				SeatCards.Add(SNew(SBox).WidthOverride(SeatWidth)[ PortsUi::Box(CardLook, Card.Build(SeatWidth - 34), FMargin(17, 20, 17, 14)) ]);
			}
			for (int32 First = 0; First < SeatCards.Num(); First += SeatColumns)
			{
				const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
				for (int32 k = First; k < FMath::Min(SeatCards.Num(), First + SeatColumns); k++) Line->AddSlot().AutoWidth().Padding(k == First ? 0 : 16, 0, 0, 0)[ SeatCards[k] ];
				Doc.Add(Line, FMargin(0, 8));
			}
			if (LobbySeats.Num() < Cfg(TEXT("players.max")))
			{
				Doc.Add(SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ PortsUi::Button(TEXT("+ Add a bot"), [this]()
					{
						if (!LobbyRoom.IsValid()) return;
						static const TCHAR* Names[] = { TEXT("House of the Anchor"), TEXT("House of the Lion"), TEXT("House of the Rose"), TEXT("House of the Star"), TEXT("House of the Ship"), TEXT("House of the Sun") };
						FString Home, Name;
						for (const FString& H : FPortsData::Get().HomeCities) if (!LobbyRoom->Seats.ContainsByPredicate([&H](const FPortsSeat& S) { return S.home == H; })) { Home = H; break; }
						for (const TCHAR* N : Names) if (!LobbyRoom->Seats.ContainsByPredicate([N](const FPortsSeat& S) { return S.name == N; })) { Name = N; break; }
						if (Name.IsEmpty()) Name = FString::Printf(TEXT("Bot %d"), LobbyRoom->Seats.Num() + 1);
						LobbyRoom->AddBot(Name, Home, TEXT("medium"));
					}, EButton::Small) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[ Rich(TEXT("A computer house, played on this screen."), TEXT("Ports.Small"), ETextJustify::Left, false) ], FMargin(0, 6));
			}
		}
	}
	if (!bDevices) Doc.Label(TEXT("Number of houses"));
	TArray<TSharedRef<SWidget>> Row;
	for (int32 N = Cfg(TEXT("players.min")); N <= Cfg(TEXT("players.max")); N++)
	{
		Row.Add(Seg(FString::Printf(TEXT("%d houses"), N), Form->count == N, [Form, N, Redraw]()
		{
			Form->count = N;
			// No two houses may share a home city.
			TArray<FString> Used;
			for (int32 i = 0; i < N; i++)
			{
				if (Used.Contains(Form->players[i].home))
				{
					for (const FString& H : FPortsData::Get().HomeCities) if (!Used.Contains(H)) { Form->players[i].home = H; break; }
				}
				Used.Add(Form->players[i].home);
			}
			Redraw();
		}));
	}
	if (!bDevices)
	{
		Doc.Row(Row, 7);
		Doc.Small(TEXT("Any house can be played by a bot, so you can also play alone."));
		Doc.Space(10);
	}

	// At most three houses to a row, as on the website.
	const int32 Columns = FMath::Min(Form->count, 3);
	const float CardWidth = (InnerWidth - 16.f * (Columns - 1)) / Columns;
	TArray<TSharedRef<SWidget>> Cards;
	for (int32 i = 0; i < (bDevices ? 0 : Form->count); i++)
	{
		FPortsSetupPlayer& P = Form->players[i];
		const FLinearColor House = Color(*P.color);
		FPortsDoc Card;
		Card.Width(CardWidth - 34);
		Card.Add(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)[ PortsUi::Crest(P.color, P.crest, 26) ]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<capsb>%s %d</>  %s"), P.bot ? TEXT("Bot") : TEXT("Player"), i + 1, *Esc(FString::Printf(TEXT("(%s, %s)"), *P.colorName, *P.crest))), TEXT("Ports.Body"), ETextJustify::Left, false) ]);
		Card.Label(TEXT("Played by"));
		Card.Row({ Seg(TEXT("A person"), !P.bot, [Form, i, Redraw]() { Form->players[i].bot = false; Redraw(); }), Seg(TEXT("A bot"), P.bot, [Form, i, Redraw]() { Form->players[i].bot = true; Redraw(); }) }, 7);
		if (P.bot)
		{
			Card.Label(TEXT("Bot skill"));
			TArray<TSharedRef<SWidget>> Skills;
			const V& AllSkills = Config.Get(TEXT("bots")).Get(TEXT("skills"));
			for (int32 k = 0; k < AllSkills.GetKeys().Num(); k++)
			{
				const FString Key = AllSkills.GetKeys()[k];
				Skills.Add(Seg(Esc(AllSkills.ValueAt(k).Get(TEXT("label")).AsString()), P.skill == Key, [Form, i, Key, Redraw]() { Form->players[i].skill = Key; Redraw(); }));
			}
			Card.Row(Skills, 7);
			Card.Small(Esc(AllSkills.Get(P.skill).Get(TEXT("description")).AsString()));
		}
		Card.Label(TEXT("House name"));
		Card.Add(PortsUi::Box(InputLook, SNew(SBox).MinDesiredHeight(30).VAlign(VAlign_Center)
			[
				SNew(SEditableText)
				.Text(FText::FromString(P.name))
				.Font(PortsUi::Serif(18))
				.ColorAndOpacity(Color(TEXT("#26190a")))
				.OnTextChanged_Lambda([Form, i](const FText& Text) { Form->players[i].name = Text.ToString().Left(24); })
			], FMargin(11, 6)));
		Card.Label(TEXT("Home city"));
		// A drop-down list of the eight home cities.
		const TSharedRef<TWeakPtr<SMenuAnchor>> AnchorRef = MakeShared<TWeakPtr<SMenuAnchor>>();
		TSharedPtr<SMenuAnchor> Anchor;
		SAssignNew(Anchor, SMenuAnchor)
			.Placement(MenuPlacement_ComboBox)
			.OnGetMenuContent_Lambda([Form, i, Redraw, CardWidth]()
			{
				FPortsDoc List;
				for (const FString& H : FPortsData::Get().HomeCities)
				{
					const bool bOn = Form->players[i].home == H;
					List.Add(SNew(SPortsButton)
						.Look(PortsUi::PlainLook(bOn ? Color(TEXT("#ecd9aa")) : FLinearColor::Transparent, 6))
						.HoverLook(PortsUi::PlainLook(Color(TEXT("#fff3c4")), 6))
						.HoverShift(FVector2D::ZeroVector).DownShift(FVector2D::ZeroVector)
						.Padding(FMargin(10, 4))
						.OnClicked([Form, i, H, Redraw]() { Form->players[i].home = H; Redraw(); })
						[
							Rich(Esc(FPortsData::Get().FindCity(H)->Modern), TEXT("Ports.Body"))
						], FMargin(0, 1));
				}
				FPortsBoxLook Look = PortsUi::PlainLook(Color(TEXT("#fffdf6")), 8, Color(TEXT("#4d3a22")), 2);
				Look.SoftShadow = 0.5f;
				return SNew(SBox).WidthOverride(CardWidth - 34)[ PortsUi::Box(Look, List.Widget(), FMargin(6), true) ];
			})
			[
				SNew(SPortsButton)
				.Look(InputLook).HoverLook(InputLook)
				.HoverShift(FVector2D::ZeroVector).DownShift(FVector2D::ZeroVector)
				.Padding(FMargin(11, 6))
				.OnClicked([AnchorRef]() { if (const TSharedPtr<SMenuAnchor> Open = AnchorRef->Pin()) Open->SetIsOpen(!Open->IsOpen()); })
				[
					SNew(SBox).MinDesiredHeight(30).VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)[ Rich(Esc(Data().FindCity(P.home)->Modern), TEXT("Ports.Body")) ]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 2, 0)[ Rich(TEXT("⌄"), TEXT("Ports.Body")) ]
					]
				]
			];
		*AnchorRef = Anchor;
		Card.Add(Anchor.ToSharedRef());
		const FPortsCity& Home = *Data().FindCity(P.home);
		const V& Bonus = Home.Raw.Get(TEXT("home"));
		const int32 Florins = Form->mode == TEXT("quick") && !Bonus.Get(TEXT("quickStartFlorins")).IsMissing() ? Bonus.Get(TEXT("quickStartFlorins")).AsInt() : Bonus.Get(TEXT("startFlorins")).AsInt();
		Card.Add(SNew(SBox).MinDesiredHeight(44)[ Rich(FString::Printf(TEXT("Plague arrives: %s. Starts with %dƒ and %d reputation."), *Esc(Home.ArrivalDateText), Cfg(TEXT("start.florins")) + Florins, Cfg(TEXT("start.reputation")) + Bonus.Get(TEXT("startReputation")).AsInt()), TEXT("Ports.Small"), ETextJustify::Left, true, CardWidth - 34) ], FMargin(0, 6, 0, 0));
		FPortsBoxLook CardLook = PortsUi::PlainLook(Color(TEXT("#fffaf0")), 12, House, 3);
		CardLook.TopBar = House;
		CardLook.TopBarHeight = 9;
		Cards.Add(SNew(SBox).WidthOverride(CardWidth)[ PortsUi::Box(CardLook, Card.Widget(), FMargin(17, 20, 17, 14)) ]);
	}
	// Rows of up to three cards, each row's cards as tall as its tallest.
	for (int32 First = 0; First < Cards.Num(); First += Columns)
	{
		const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 k = First; k < FMath::Min(Cards.Num(), First + Columns); k++) Line->AddSlot().AutoWidth().Padding(k == First ? 0 : 16, 0, 0, 0)[ Cards[k] ];
		Doc.Add(Line, FMargin(0, 8));
	}
	Doc.Space(6);

	// Game length and difficulty, side by side.
	const int32 PreRounds = Cfg(*(TEXT("prePlague.rounds.") + Form->mode));
	const int32 Houses = bDevices ? FMath::Max(LobbySeats.Num(), Cfg(TEXT("players.min"))) : Form->count;
	int32 Estimate = Cfg(*FString::Printf(TEXT("timeEstimates.%s.%d"), *Form->mode, Houses));
	const int32 FullEstimate = Estimate;
	// The estimates in config.json assume the default options (pre-plague rounds and the timer on).
	if (!Form->prePlague) Estimate -= FMath::RoundToInt32(Estimate * static_cast<double>(PreRounds) / (PreRounds + static_cast<double>(Cfg(TEXT("rounds"))) / Cfg(*(TEXT("modes.") + Form->mode + TEXT(".span")))));
	FPortsDoc Length;
	Length.Width(InnerWidth / 2 - 8);
	Length.Label(TEXT("Game length"));
	Row.Reset();
	const V& Modes = Config.Get(TEXT("modes"));
	for (int32 k = 0; k < Modes.GetKeys().Num(); k++)
	{
		const FString Key = Modes.GetKeys()[k];
		Row.Add(Seg(Esc(Modes.ValueAt(k).Get(TEXT("label")).AsString()), Form->mode == Key, [Form, Key, Redraw]() { Form->mode = Key; Redraw(); }));
	}
	Length.Row(Row, 7);
	Length.Small(FString::Printf(TEXT("%s %d action points per turn."), *Esc(Modes.Get(Form->mode).Get(TEXT("description")).AsString()), Cfg(*(TEXT("modes.") + Form->mode + TEXT(".actionPoints")))));
	Length.Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#efe2bf")), 13), Rich(FString::Printf(TEXT("About %d minutes for %d players%s"), Estimate, Houses, Form->timer ? TEXT("") : TEXT(" (longer without the turn timer)")), TEXT("Ports.Small"), ETextJustify::Left, false), FMargin(10, 2)) ]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Rich(FullEstimate > 60 && Form->mode == TEXT("standard") ? TEXT("· Quick Play is recommended for this many players.") : TEXT(""), TEXT("Ports.Small"), ETextJustify::Left, false) ]);
	FPortsDoc Difficulty;
	Difficulty.Width(InnerWidth / 2 - 8);
	Difficulty.Label(TEXT("Difficulty"));
	Row.Reset();
	const V& Difficulties = Config.Get(TEXT("difficulty"));
	for (int32 k = 0; k < Difficulties.GetKeys().Num(); k++)
	{
		const FString Key = Difficulties.GetKeys()[k];
		Row.Add(Seg(Esc(Difficulties.ValueAt(k).Get(TEXT("label")).AsString()), Form->difficulty == Key, [Form, Key, Redraw]() { Form->difficulty = Key; Redraw(); }));
	}
	Difficulty.Row(Row, 7);
	Difficulty.Small(Form->difficulty == TEXT("apprentice") ? TEXT("Apprentice: the plague is gentler (severity rolls 1 lower) and hints stay on all game.")
		: Form->difficulty == TEXT("mortality") ? TEXT("Great Mortality: severity rolls are 1 higher and every shipment has +1 contagion risk. For experienced merchants.")
		: TEXT("Chronicler: the standard game."));
	Doc.Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[ Length.Widget() ]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)[ Difficulty.Widget() ]);
	Doc.Space(8);

	// The three options, each a tick box in its own pale panel.
	const float CheckText = (InnerWidth - 20) / 3 - 20 - 33;
	const auto Check = [CheckText](const FString& Title, const FString& Text, bool bOn, TFunction<void()> Toggle) -> TSharedRef<SWidget>
	{
		const FPortsBoxLook Panel = PortsUi::PlainLook(FLinearColor(1.f, 0.955f, 0.83f, 0.6f), 10, Color(TEXT("#d8bc7c")), 1);
		const FPortsBoxLook Tick = bOn ? PortsUi::PlainLook(Color(TEXT("#1f6b4f")), 4) : PortsUi::PlainLook(FLinearColor::White, 4, Color(TEXT("#4d3a22")), 1.5f);
		return SNew(SPortsButton)
			.Look(Panel).HoverLook(Panel)
			.HoverShift(FVector2D::ZeroVector).DownShift(FVector2D::ZeroVector)
			.Padding(FMargin(10, 8))
			.OnClicked(Toggle)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 9, 0)
				[
					SNew(SBox).WidthOverride(24).HeightOverride(24)[ PortsUi::Box(Tick, SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Rich(bOn ? TEXT("<lb>✓</>") : TEXT(""), TEXT("Ports.Body")) ]) ]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Rich(FString::Printf(TEXT("<capsb>%s</>"), *Title), TEXT("Ports.Body"), ETextJustify::Left, false) ]
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Text, TEXT("Ports.Small"), ETextJustify::Left, true, CheckText) ]
				]
			];
	};
	const FString FirstPre = Data().Timeline().Get(TEXT("prePlague"))[0].Get(TEXT("label")).AsString();
	Doc.Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)
		[
			Check(PreRounds > 1 ? TEXT("Pre-plague rounds") : TEXT("Pre-plague round"),
				FString::Printf(TEXT("%s before the plague arrives (from %s): no Event card, no plague, and trading posts cost %dƒ less."), PreRounds > 1 ? *FString::Printf(TEXT("%d extra rounds"), PreRounds) : TEXT("One extra round"), *Esc(FirstPre), Cfg(TEXT("prePlague.postDiscount"))),
				Form->prePlague, [Form, Redraw]() { Form->prePlague = !Form->prePlague; Redraw(); })
		]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(5, 0)
		[
			Check(TEXT("Turn timer"), FString::Printf(TEXT("%d seconds per turn; when time runs out, the next house plays. The clock stops while cards are shown."), Cfg(TEXT("turnTimer.seconds"))),
				Form->timer, [Form, Redraw]() { Form->timer = !Form->timer; Redraw(); })
		]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(5, 0, 0, 0)
		[
			Check(TEXT("Guided hints"), TEXT("Tips on screen during the first round."), Form->hints, [Form, Redraw]() { Form->hints = !Form->hints; Redraw(); })
		]);

	Doc.P(Form->error.IsEmpty() ? FString(TEXT(" ")) : FString::Printf(TEXT("<risk>%s</>"), *Esc(Form->error)));
	Doc.Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ PortsUi::Button(TEXT("← Back"), [this]() { ShowMenu(); }, EButton::Ghost) ]
		+ SHorizontalBox::Slot().FillWidth(1)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			PortsUi::Button(TEXT("Roll for turn order →"), [this, Form, Redraw]()
			{
				FPortsSetup Setup;
				if (Form->bDevices)
				{
					// The houses are the room's seats, in the order they joined; each takes the next colour and crest.
					if (!LobbyRoom.IsValid() || !LobbyRoom->bReady) { Form->error = TEXT("The room is not open yet."); Redraw(); return; }
					for (int32 i = 0; i < LobbyRoom->Seats.Num(); i++)
					{
						const FPortsSeat& S = LobbyRoom->Seats[i];
						FPortsSetupPlayer P;
						P.name = S.name; P.home = S.home; P.bot = S.bot; P.skill = S.skill.IsEmpty() ? FString(TEXT("medium")) : S.skill;
						P.color = Ports::PLAYER_STYLES[i].color; P.colorName = Ports::PLAYER_STYLES[i].colorName; P.crest = Ports::PLAYER_STYLES[i].crest;
						Setup.players.Add(P);
					}
					if (Setup.players.Num() < Cfg(TEXT("players.min")))
					{
						Form->error = FString::Printf(TEXT("At least %d houses are needed: wait for players to join, or add a bot."), Cfg(TEXT("players.min")));
						Redraw();
						return;
					}
				}
				else for (int32 i = 0; i < Form->count; i++)
				{
					FPortsSetupPlayer P = Form->players[i];
					P.name = P.name.TrimStartAndEnd();
					Setup.players.Add(P);
				}
				Form->error = Ports::ValidateSetup(Setup.players);
				if (!Form->error.IsEmpty()) { Redraw(); return; }
				Setup.difficulty = Form->difficulty;
				Setup.mode = Form->mode;
				Setup.prePlague = Form->prePlague;
				Setup.timer = Form->timer;
				Setup.seed = FString::Printf(TEXT("%lld-%f"), FDateTime::UtcNow().ToUnixTimestamp() * 1000 + FDateTime::UtcNow().GetMillisecond(), FMath::FRand());
				if (Form->bDevices)
				{
					// The lobby's room becomes the game's room.
					Room = LobbyRoom;
					LobbyRoom.Reset();
					Room->bStarted = true;
					AdoptRoom();
					Form->bDevices = false;
				}
				BeginGame(Setup, Form->hints || Form->difficulty == TEXT("apprentice"));
			}, EButton::Primary)
		]);
	// A change on this screen redraws only what is inside the frame, so the page neither jumps nor scrolls back to the top.
	if (const TSharedPtr<SBox> Holder = SetupHolder.Pin())
	{
		Holder->SetContent(Doc.Widget());
		return;
	}
	const TSharedRef<SBox> Holder = SNew(SBox)[ Doc.Widget() ];
	SetupHolder = Holder;
	Root->SetScreen(Page(Holder, 1180, TEXT("bg_wood"), true, Root));
}

void UPortsGameFlow::BeginGame(const FPortsSetup& Setup, bool bHints)
{
	FString Problem;
	if (!Ports::CreateGame(Setup, State, Problem)) { Notify(Problem, 5.f); return; }
	Ui = FPortsUiState();
	Ui.hints = bHints;
	Save();
	EnterGame();
}

void UPortsGameFlow::ContinueSaved()
{
	if (!LoadSaved(State, Ui)) { Notify(TEXT("The saved game could not be opened.")); return; }
	// A saved multi-device game reopens its room, so the players' devices can rejoin.
	if (!Ui.roomCode.IsEmpty()) { ReopenRoom(); return; }
	EnterGame();
}

// The game's room, once it has one: seats coming and going are saved and shown, and requests from devices are played.
void UPortsGameFlow::AdoptRoom()
{
	Room->OnChange = [this]()
	{
		if (!bInGame) return;
		Save();
		Refresh();
		// A house that has just left during its own turn is skipped as soon as nothing else is going on.
		Pump();
	};
	Room->OnIntent = [this](const V& Message) { HandleIntent(Message); };
}

void UPortsGameFlow::ReopenRoom()
{
	const auto Message = [this](const FString& Title, const FString& Text, bool bButtons)
	{
		FPortsDoc Doc;
		Doc.H1(Title);
		Doc.P(Text);
		if (bButtons)
		{
			Doc.Space(8);
			Doc.Add(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ PortsUi::Button(TEXT("← Menu"), [this]() { ShowMenu(); }, EButton::Ghost) ]
				+ SHorizontalBox::Slot().FillWidth(1)[ SNew(SSpacer) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[ PortsUi::Button(TEXT("Play on this device"), [this]() { Ui.roomCode.Reset(); EnterGame(); }) ]
				+ SHorizontalBox::Slot().AutoWidth()[ PortsUi::Button(TEXT("Try again"), [this]() { ReopenRoom(); }, EButton::Primary) ]);
		}
		Root->SetScreen(Page(Doc.Build(700 - 58), 700, TEXT("bg_wood"), true, Root, true));
	};
	Message(FString::Printf(TEXT("Room %s"), *Ui.roomCode), TEXT("Reopening the room for the players’ devices…"), false);
	const auto Failed = [Message]() { Message(TEXT("Could not reopen the room"), TEXT("Check the internet connection. You can also finish this game on this device only."), true); };
	if (!FPortsTransport::IsAvailable()) { Failed(); return; }
	const TSharedRef<FPortsRoom> Opening = MakeShared<FPortsRoom>();
	LobbyRoom = Opening;
	TWeakObjectPtr<UPortsGameFlow> Weak(this);
	Opening->Open(Ui.roomCode, FPortsRoom::SeatsFromValue(Ui.roomSeats), true, [Weak, Failed](bool bOk)
	{
		if (!Weak.IsValid() || !Weak->LobbyRoom.IsValid()) return;
		if (!bOk) { Weak->LobbyRoom.Reset(); Failed(); return; }
		// If another big screen had taken the old code, the room has a new one, shown on the side panel.
		Weak->Room = Weak->LobbyRoom;
		Weak->LobbyRoom.Reset();
		Weak->Ui.roomCode = Weak->Room->Code;
		Weak->AdoptRoom();
		Weak->EnterGame();
	});
}

void UPortsGameFlow::EnterGame()
{
	bInGame = true;
	HandledTurn = -1;
	Steps.Reset();
	bBotWaiting = false;
	bClockOn = false;
	OpenPrompt.Reset();
	Selectable.Reset();
	bResults = false;
	bPlagueCard = false;
	// The song for the game as it stands: the menu song plays on until the first round's opening card.
	RoundSong = GameSong();
	Root->SetScreen(BuildGameScreen());
	SetSidePanel(Map, true);
	Refresh();
	PushToDevices();
	Pump();
}

void UPortsGameFlow::LeaveGame()
{
	Save();
	bInGame = false;
	bPlagueCard = false;
	bBotWaiting = false;
	bClockOn = false;
	Steps.Reset();
	Root->CloseAllSilently();
	ShowMenu();
}

// ---------- The game screen ----------

TSharedRef<SWidget> UPortsGameFlow::BuildGameScreen()
{
	const TSharedRef<SWidget> Top = SAssignNew(TopBarSlot, SBox);
	// The side panel sits on the walnut table, as the website's does. Its scroll bar lies over the
	// panel's right margin, so the panels are the same width whether or not there is anything to scroll.
	const TSharedRef<SScrollBar> SideBar = SNew(SScrollBar);
	const TSharedRef<SWidget> Side = SNew(SBox).WidthOverride(SideWidth)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(PortsUi::PictureBrush(TEXT("bg_wood"))) ]
			+ SOverlay::Slot()
			[
				SAssignNew(SideScroll, SScrollBox).ExternalScrollbar(SideBar)
				+ SScrollBox::Slot().Padding(FMargin(SidePad, 13, SidePad, 16))[ SAssignNew(SidebarSlot, SBox) ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right)[ SideBar ]
		];
	Root->MarkSolid(Top);
	Root->MarkSolid(Side);
	// Map buttons, as on the website's map: closer, further, and the whole map again.
	const auto MapButton = [this](const FString& Label, TFunction<void(APortsCameraPawn&)> Do)
	{
		FPortsBoxLook Look = PortsUi::PlainLook(FLinearColor(0.96f, 0.91f, 0.77f, 0.95f), 8, Color(TEXT("#d9a82b")), 2);
		Look.SoftShadow = 0.35f;
		FPortsBoxLook Hover = Look;
		Hover.Top = Hover.Bottom = Color(TEXT("#fffaf0"));
		return SNew(SPortsButton).Look(Look).HoverLook(Hover).HoverShift(FVector2D::ZeroVector).DownShift(FVector2D(0, 1)).Padding(FMargin(10, 2))
			.OnClicked([this, Do]()
			{
				APlayerController* PC = Map && Map->GetWorld() ? Map->GetWorld()->GetFirstPlayerController() : nullptr;
				if (APortsCameraPawn* Pawn = PC ? Cast<APortsCameraPawn>(PC->GetPawn()) : nullptr) Do(*Pawn);
			})
			[
				SNew(SBox).MinDesiredWidth(16).MinDesiredHeight(30).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Rich(Label, TEXT("Ports.Body"), ETextJustify::Center, false) ]
			];
	};
	const TSharedRef<SWidget> MapButtons = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 5)[ MapButton(TEXT("<b>+</>"), [](APortsCameraPawn& Pawn) { Pawn.ZoomBy(1.5); }) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 5)[ MapButton(TEXT("<b>−</>"), [](APortsCameraPawn& Pawn) { Pawn.ZoomBy(1 / 1.5); }) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[ MapButton(TEXT("Reset map  <key>H</>"), [](APortsCameraPawn& Pawn) { Pawn.ShowWholeMap(); }) ];
	Root->MarkSolid(MapButtons);

	// .legend in game.css and map.js: bottom left of the map, folded until its heading is clicked.
	const TSharedRef<SBox> LegendHolder = SNew(SBox);
	const TSharedRef<bool> bLegendOpen = MakeShared<bool>(bTestLegend);
	const TSharedRef<TFunction<void()>> DrawLegend = MakeShared<TFunction<void()>>();
	TWeakPtr<SBox> WeakLegend = LegendHolder;
	TWeakPtr<TFunction<void()>> WeakDraw = DrawLegend;
	*DrawLegend = [WeakLegend, WeakDraw, bLegendOpen]()
	{
		const TSharedPtr<SBox> Holder = WeakLegend.Pin();
		if (!Holder.IsValid()) return;
		const auto Line = [](const TCHAR* Icon, const FVector2D& Size, const TCHAR* Text) -> TSharedRef<SWidget>
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)[ SNew(SBox).WidthOverride(30).HAlign(HAlign_Center)[ PortsUi::Picture(Icon, Size) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Rich(Text, TEXT("Ports.Small"), ETextJustify::Left, false) ];
		};
		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SPortsButton).Look(PortsUi::PlainLook(FLinearColor::Transparent, 0)).HoverLook(PortsUi::PlainLook(FLinearColor::Transparent, 0)).HoverShift(FVector2D::ZeroVector).DownShift(FVector2D::ZeroVector)
				.OnClicked([WeakDraw, bLegendOpen]()
				{
					*bLegendOpen = !*bLegendOpen;
					if (const TSharedPtr<TFunction<void()>> Draw = WeakDraw.Pin()) (*Draw)();
				})
				[
					Rich(*bLegendOpen ? TEXT("<legend>Legend  \u25BC</>") : TEXT("<legend>Legend  \u25B6</>"), TEXT("Ports.Small"), ETextJustify::Left, false)
				]
			];
		if (*bLegendOpen)
		{
			Rows->AddSlot().AutoHeight().Padding(0, 4, 0, 1)[ Line(TEXT("legend_safe"), FVector2D(30, 25), TEXT("Safe city")) ];
			Rows->AddSlot().AutoHeight().Padding(0, 1)[ Line(TEXT("legend_threatened"), FVector2D(30, 25), TEXT("Threatened (next to plague)")) ];
			Rows->AddSlot().AutoHeight().Padding(0, 1)[ Line(TEXT("legend_stricken"), FVector2D(30, 25), TEXT("Stricken (pips = severity)")) ];
			Rows->AddSlot().AutoHeight().Padding(0, 1)[ Line(TEXT("legend_aftermath"), FVector2D(30, 25), TEXT("Aftermath")) ];
			Rows->AddSlot().AutoHeight().Padding(0, 1)[ Line(TEXT("legend_post"), FVector2D(30, 30), TEXT("Trading post (number = family)")) ];
			Rows->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Line(TEXT("legend_sea"), FVector2D(30, 11.5), TEXT("Sea route")) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Line(TEXT("legend_land"), FVector2D(30, 11.5), TEXT("Land")) ]
			];
		}
		FPortsBoxLook Look = PortsUi::PlainLook(FLinearColor(0.984f, 0.961f, 0.894f, 0.95f), 10, Color(TEXT("#d9a82b")), 2);
		Look.SoftShadow = 0.35f;
		Holder->SetContent(PortsUi::Box(Look, Rows, FMargin(12, 7), true));
	};
	(*DrawLegend)();
	LegendKeep = DrawLegend;
	Root->MarkSolid(LegendHolder);

	// The map sits in a gold frame set into the table (.map-wrap and .map-frame in game.css).
	const float In = SPortsMapFrame::Band + 10.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Top ]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SPortsMapFrame).Visibility(EVisibility::HitTestInvisible) ]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(In, In, 0, 0)[ MapButtons ]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(In, 0, 0, In)[ LegendHolder ]
			]
			+ SHorizontalBox::Slot().AutoWidth()[ Side ]
		];
}

void UPortsGameFlow::Refresh()
{
	if (!Root.IsValid() || !bInGame) return;
	RefreshTopBar();
	if (SidebarSlot.IsValid())
	{
		const TSharedRef<SWidget> Side = BuildSidebar();
		SidebarSlot->SetContent(Side);
		Root->Watch(Side, TEXT("side panel"));
	}
	if (Map) Map->ApplyState(State, Selectable, HighlightRoutes);
}

void UPortsGameFlow::RefreshTopBar()
{
	if (TopBarSlot.IsValid() && bInGame) TopBarSlot->SetContent(BuildTopBar());
}

// .topbar in game.css: deep red, with a gold line under it.
TSharedRef<SWidget> UPortsGameFlow::BuildTopBar()
{
	const FPortsRoundInfo Info = Ports::RoundInfo(State);
	const FString& Ph = State.phase;
	const TCHAR* Phase = Ph == TEXT("roundStart") ? TEXT("Prologue") : Ph == TEXT("chronicle") ? TEXT("Chronicle") : Ph == TEXT("event") ? TEXT("Event") : Ph == TEXT("actions") ? TEXT("Actions") : Ph == TEXT("plague") ? TEXT("Plague & upkeep") : TEXT("Game over");
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	const TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[ Rich(TEXT("Ports of Plague"), TEXT("Ports.Brand"), ETextJustify::Left, false) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Rich(Info.bValid ? Esc(Info.label) : FString(TEXT("1346")), TEXT("Ports.Date"), ETextJustify::Left, false) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Rich(Info.bValid ? FString::Printf(TEXT("%s · %sRound %d of %d · %s"), *Esc(Info.months), Info.pre ? TEXT("Before the plague · ") : TEXT(""), Ports::RoundNumber(State), Ports::TotalRounds(State), Phase) : FString(TEXT("Prologue")), TEXT("Ports.DateSmall"), ETextJustify::Left, false)
			]
		];
	// One circle for each round (.timeline in game.css): an anchor for each round before the plague, then the
	// half-years by their season, or a star for each of Quick Play's longer rounds. Rounds played are filled,
	// and the present one is bright and a little larger.
	{
		const TSharedRef<SHorizontalBox> Track = SNew(SHorizontalBox);
		const int32 Span = FMath::Max(1, State.span);
		const int32 PreSize = State.preRounds ? FMath::Max(1, (1 - State.firstHalf) / State.preRounds) : 1;
		const auto Mark = [&](int32 First, int32 Last, const TCHAR* Icon, bool bGapAfter)
		{
			const bool bDone = Last < State.round, bNow = First <= State.round && State.round <= Last;
			const float Size = bNow ? 21.6f : 18.f;
			FPortsBoxLook Look = PortsUi::PlainLook(Color(bNow ? TEXT("#f3d27a") : bDone ? TEXT("#7a560c") : TEXT("#3a0806")), Size * 0.5f, Color(TEXT("#d9a82b")), 2);
			Track->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, bGapAfter ? 9 : 2, 0)
			[
				SNew(SBox).WidthOverride(Size).HeightOverride(Size)
				[
					PortsUi::Box(Look, SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[ PortsUi::Picture(FString::Printf(TEXT("track_%s_%s"), Icon, bNow ? TEXT("on") : TEXT("off")), FVector2D(Size, Size)) ])
				]
			];
		};
		if (State.preRounds) for (const V& R : Data().Timeline().Get(TEXT("prePlague")).GetItems())
		{
			const int32 Round = R.Get(TEXT("round")).AsInt();
			if (Round < State.firstHalf || (Round - State.firstHalf) % PreSize != 0) continue;
			const int32 Last = Round + PreSize - 1;
			Mark(Round, Last, TEXT("anchor"), Last == 0);
		}
		for (const V& R : Data().Timeline().Get(TEXT("rounds")).GetItems())
		{
			const int32 Round = R.Get(TEXT("round")).AsInt();
			if ((Round - 1) % Span != 0) continue;
			const int32 Last = FMath::Min(Round + Span - 1, Cfg(TEXT("rounds")));
			Mark(Round, Last, Span > 1 ? TEXT("star") : R.Get(TEXT("season")).AsString() == TEXT("warm") ? TEXT("sun") : TEXT("snow"), false);
		}
		Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[ Track ];
	}

	// Whose turn it is, in a pale pill ringed with gold.
	FPortsBoxLook Pill = PortsUi::PlainLook(FLinearColor(1.f, 0.94f, 0.76f, 0.95f), 18, Color(TEXT("#d9a82b")), 2);
	const TSharedRef<SHorizontalBox> Turn = SNew(SHorizontalBox);
	if (P)
	{
		Turn->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ PortsUi::Crest(*P, 20) ];
		Turn->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<b>%s's turn</>%s"), *Esc(P->name), P->bot ? TEXT(" <small>(bot)</>") : TEXT("")), TEXT("Ports.Body"), ETextJustify::Left, false) ];
	}
	else Turn->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("<b>%s</>"), Phase), TEXT("Ports.Body"), ETextJustify::Left, false) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ PortsUi::Box(Pill, Turn, FMargin(14, 3)) ];
	if (bClockOn && P)
	{
		const int32 Secs = FMath::Max(0, FMath::CeilToInt32(ClockLeft));
		const bool bPaused = Root->HasStoryDialog() || (Map && Map->IsAnimating()) || Steps.Num() > 0;
		const bool bUrgent = Secs <= Cfg(TEXT("turnTimer.warnAt")) && !bPaused;
		FPortsBoxLook Clock = PortsUi::PlainLook(bUrgent ? Color(TEXT("#b0261a")) : Color(TEXT("#3a0806")), 18, bUrgent ? Color(TEXT("#f3d27a")) : Color(*P->color), 2);
		Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			PortsUi::Box(Clock, SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)[ PortsUi::Crest(*P, 16) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::Printf(TEXT("%s%ds"), bPaused ? TEXT("paused · ") : TEXT(""), Secs), TEXT("Ports.BtnSmallGold"), ETextJustify::Left, false) ], FMargin(12, 3))
		];
	}
	Bar->AddSlot().FillWidth(1)[ SNew(SSpacer) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)[ PortsUi::Button(TEXT("Rules <key>R</>"), [this]() { ShowRules(); }, EButton::Small) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)[ PortsUi::Button(TEXT("Journal <key>J</>"), [this]() { ShowJournal(); }, EButton::Small) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)[ PortsUi::Button(bSoundOn ? TEXT("Sound on <key>M</>") : TEXT("Sound off <key>M</>"), [this]() { SetSoundOn(!bSoundOn); Notify(bSoundOn ? TEXT("Sound effects on") : TEXT("Sound effects off"), 1.2f); RefreshTopBar(); }, EButton::Small) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)[ PortsUi::Button(bMusicOn ? TEXT("Music on <key>N</>") : TEXT("Music off <key>N</>"), [this]() { SetMusicOn(!bMusicOn); Notify(bMusicOn ? TEXT("Music on") : TEXT("Music off"), 1.2f); RefreshTopBar(); }, EButton::Small) ];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)[ PortsUi::Button(TEXT("Save & menu"), [this]() { LeaveGame(); }, EButton::SmallGhostLight) ];
	FPortsBoxLook Look;
	Look.Top = Color(TEXT("#8f1a12")); Look.Mid = Color(TEXT("#5c0d09")); Look.MidAt = 0.7f; Look.Bottom = Color(TEXT("#470a07"));
	Look.Radius = 0;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ PortsUi::Box(Look, Bar, FMargin(14, 5), true) ]
		+ SVerticalBox::Slot().AutoHeight()[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#d9a82b")), 0), SNew(SBox).HeightOverride(3)) ];
}

// The big screen of a multi-device game: the side panel always fits, with nothing to scroll (fitScreen in
// game.js). It first leaves out the oldest chronicle lines, then the chronicle, then the historical note,
// and only then shrinks.
TSharedRef<SWidget> UPortsGameFlow::BuildSidebar()
{
	if (!Remote() || !SideScroll.IsValid()) return BuildSidebarWith(10, true, 1.f);
	const float Seen = SideScroll->GetCachedGeometry().GetLocalSize().Y;
	const float Space = (Seen > 100.f ? Seen : Root->ViewHeight() - 56.f) - 13.f - 16.f;
	const float Scale = Root->LayoutScale();
	TSharedPtr<SWidget> Built;
	float Tall = 0;
	const int32 Lines[] = { 10, 8, 6, 4, 3, 2, 0, -1 };
	for (const int32 Count : Lines)
	{
		Built = BuildSidebarWith(FMath::Max(0, Count), Count >= 0, 1.f);
		Built->SlatePrepass(Scale);
		Tall = Built->GetDesiredSize().Y;
		if (Tall <= Space) return Built.ToSharedRef();
	}
	const float Zoom = FMath::Clamp(Space / FMath::Max(1.f, Tall), 0.55f, 1.f);
	return SNew(SPortsZoom).Zoom(Zoom)[ BuildSidebarWith(0, false, Zoom) ];
}

TSharedRef<SWidget> UPortsGameFlow::BuildSidebarWith(int32 LogLines, bool bNote, float Zoom)
{
	FPortsDoc Doc;
	const auto Gap = FMargin(0, 0, 0, 13);
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	const V& Skills = Data().Config().Get(TEXT("bots")).Get(TEXT("skills"));
	if (Remote())
	{
		// .room-chip: the room code stays in view all game, so a device that dropped out can find its way back.
		FPortsBoxLook Chip;
		Chip.Top = Color(TEXT("#8f1a12")); Chip.Bottom = Color(TEXT("#5c0d09"));
		Chip.Radius = 12;
		Chip.Border = Color(TEXT("#d9a82b")); Chip.BorderWidth = 2;
		Doc.Add(PortsUi::Box(Chip, Rich(FString::Printf(TEXT("<chip>Room </><chipcode>%s</><chipat>  · join at %s</>"), *Esc(Room->Code), *Esc(FPortsTransport::JoinAddress())), TEXT("Ports.Body"), ETextJustify::Left, false), FMargin(13, 5)), Gap);
	}
	if (P)
	{
		AddHousePanel(Doc, *P);
		if (!P->bot && Remote())
		{
			const FPortsSeat* Seat = Room->Seats.IsValidIndex(P->id) ? &Room->Seats[P->id] : nullptr;
			FPortsDoc Wait;
			if (Seat && Seat->left) Wait.P(FString::Printf(TEXT("<b>%s</> has left the game. Their turn is skipped."), *Esc(P->name)));
			else Wait.P(FString::Printf(TEXT("<b>%s</> is choosing on their own device."), *Esc(P->name)));
			if (Seat && !Seat->online && !Seat->left) Wait.P(FString::Printf(TEXT("<risk>This device is not connected. Open %s and join room </><b>%s</><risk> with the house name “%s”.</>"), *Esc(FPortsTransport::JoinAddress()), *Esc(Room->Code), *Esc(P->name)));
			Doc.Panel(TEXT("Actions"), Color(TEXT("#1d4a86")), Wait, Gap);
		}
		else if (!P->bot)
		{
			const FString Hint = HintFor(*P);
			// .hint in game.css: pale blue with a blue edge.
			if (!Hint.IsEmpty())
			{
				FPortsDoc Words;
				Words.Text(FString::Printf(TEXT("<lapis>Hint:</> %s"), *Hint), TEXT("Ports.Small"), ETextJustify::Left, FMargin(0));
				Doc.Boxed(PortsUi::PlainLook(Color(TEXT("#eef3fb")), 12, Color(TEXT("#1d4a86")), 2), Words, FMargin(12, 8), Gap);
			}
			AddActionsPanel(Doc, *P);
		}
		else
		{
			FPortsDoc Wait;
			Wait.P(FString::Printf(TEXT("<b>%s</> is a computer player (%s). It is taking its turn…"), *Esc(P->name), *Esc(Skills.Get(P->skill).Get(TEXT("label")).AsString())));
			Doc.Panel(TEXT("Actions"), Color(TEXT("#1d4a86")), Wait, Gap);
		}
	}
	else
	{
		FPortsDoc Wait;
		Wait.P(Remote() ? TEXT("Read the cards as they appear. Anyone can press Next on their device. Players take turns in the order rolled at the start.") : TEXT("Read the cards as they appear. Players take turns in the order rolled at the start."));
		Doc.Panel(State.phase == TEXT("plague") ? TEXT("The plague takes its toll") : TEXT("The chronicle unfolds"), Color(TEXT("#7a1410")), Wait, Gap);
	}
	if (Ui.lastNote.Num() && bNote)
	{
		FPortsDoc Note;
		for (const FString& Id : Ui.lastNote) Note.Fact(Id);
		Doc.Panel(TEXT("Latest Historical Note"), Color(TEXT("#1f6b4f")), Note, Gap);
	}
	FPortsDoc Houses;
	for (int32 i = 0; i < State.order.Num(); i++)
	{
		const FPortsPlayer& H = State.players[State.order[i]];
		const FPortsSeat* HSeat = Remote() && Room->Seats.IsValidIndex(H.id) ? &Room->Seats[H.id] : nullptr;
		// A bot is marked as one; on several devices a house shows whether its device is connected (green) or not, or that it has left.
		const FString Tag = H.bot ? FString::Printf(TEXT("  <small>Bot · %s</>"), *Esc(Skills.Get(H.skill).Get(TEXT("label")).AsString()))
			: !HSeat ? FString() : HSeat->left ? FString(TEXT("  <small>(left)</>")) : HSeat->online ? FString(TEXT("  <doton>\u25CF</>")) : FString(TEXT("  <dotoff>\u25CF</>"));
		const FString Name = FString::Printf(TEXT("<sb>%d. %s</>%s"), i + 1, *Esc(H.name), *Tag);
		const FString Numbers = FString::Printf(TEXT("%dƒ · rep %d · family %d · %d post%s"), H.florins, H.reputation, Ports::FamilyTotal(H), H.posts.Num(), H.posts.Num() > 1 ? TEXT("s") : TEXT(""));
		const FString Total = FString::Printf(TEXT("<b>%d</>"), Ports::ScorePlayer(H).total);
		// .house-row: a bar in the house's colour; the house whose turn it is glows gold.
		const bool bCurrent = P && P->id == H.id;
		FPortsBoxLook Look = PortsUi::PlainLook(bCurrent ? FLinearColor(0.69f, 0.39f, 0.02f, 0.28f) : FLinearColor(1, 1, 1, 0.4f), 10, Color(TEXT("#d9a82b")), bCurrent ? 2.f : 0.f);
		Look.LeftBar = Color(*H.color);
		Look.LeftBarWidth = 5;
		const FString Crest = H.crest, Colour = H.color;
		Houses.AddBuilt([Look, Name, Numbers, Total, Crest, Colour](float W)
		{
			// The score keeps a fixed room on the right, so the lines beside it know their width at once.
			const float ScoreWidth = 38.f;
			const float TextWidth = W > 0.f ? FMath::Max(80.f, W - 20.f - 28.f - ScoreWidth) : 0.f;
			return PortsUi::Box(Look, SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ PortsUi::Crest(Colour, Crest, 20) ]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Name, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ]
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Numbers, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(ScoreWidth).HAlign(HAlign_Right)[ Rich(Total, TEXT("Ports.Body"), ETextJustify::Right, false) ] ], FMargin(12, 4, 8, 4));
		}, FMargin(0, 3));
	}
	Doc.Panel(TEXT("Houses (turn order)"), Color(TEXT("#5b2a86")), Houses, Gap);
	FPortsDoc Log;
	TArray<FString> Lines;
	for (int32 i = State.log.Num() - 1; i >= 0 && Lines.Num() < LogLines; i--)
	{
		const V& E = State.log[i];
		if (!E.Get(TEXT("text")).Truthy() || E.Get(TEXT("type")).AsString() == TEXT("turn") || E.Get(TEXT("seq")).AsInt() > Ui.seenSeq) continue;
		Lines.Add(Esc(E.Get(TEXT("text")).AsString()));
	}
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		const FString Number = FString::Printf(TEXT("%d."), Lines.Num() - i);
		const FString Line = Lines[i];
		Log.AddBuilt([Number, Line](float W)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[ SNew(SBox).WidthOverride(22)[ Rich(Number, TEXT("Ports.Small"), ETextJustify::Left, false) ] ]
				+ SHorizontalBox::Slot().FillWidth(1)[ Rich(Line, TEXT("Ports.Small"), ETextJustify::Left, true, W > 0.f ? FMath::Max(80.f, W - 28.f) : 0.f) ];
		}, FMargin(0, 2));
	}
	if (LogLines > 0) Doc.Panel(TEXT("Chronicle"), Color(TEXT("#6b4a1f")), Log, Gap);
	// Shrunk, the panel is laid out wider by as much, so it still fills its column.
	return Doc.Build((SideWidth - SidePad * 2) / Zoom);
}

// ---------- Final scores ----------

void UPortsGameFlow::ShowEnd()
{
	bInGame = false;
	TestCheckResult();
	// The ending song plays on through the results, until the menu or a new game.
	bResults = true;
	bPlagueCard = false;
	bClockOn = false;
	if (!bAutoPlay) ClearSave();
	if (Map) Map->ApplyState(State);
	SetSidePanel(Map, false);
	if (!State.finalScores.IsSet() || !State.winner.IsSet()) { ShowMenu(); return; }
	const V& Skills = Data().Config().Get(TEXT("bots")).Get(TEXT("skills"));

	// The facts about this game (summary in finale.js).
	const int32 TotalStart = State.players.Num() * Cfg(TEXT("start.family"));
	int32 Lost = 0, Infected = 0;
	TArray<FString> Early, Protectors, Winners;
	for (const FPortsPlayer& P : State.players) { Lost += P.lostFamily; if (P.stats.protectedCount > 0) Protectors.Add(Esc(P.name)); }
	for (const V& E : State.log)
	{
		const FString Type = E.Get(TEXT("type")).AsString();
		if (Type == TEXT("arrival") && E.Get(TEXT("early")).Truthy()) Early.Add(Esc(CityName(E.Get(TEXT("city")).AsString())));
		if (Type == TEXT("ship") && E.Get(TEXT("infected")).Truthy()) Infected++;
	}
	for (const int32 Id : *State.winner) Winners.Add(Esc(State.players[Id].name));

	FPortsDoc Doc;
	Doc.Text(TEXT("Anno Domini 1353"), TEXT("Ports.TitleSmall"), ETextJustify::Center, FMargin(0));
	Doc.Text(FString::Printf(TEXT("%s %s with the greatest Legacy."), *FString::Join(Winners, TEXT(" and ")), Winners.Num() > 1 ? TEXT("share the victory") : TEXT("wins")), TEXT("Ports.Subtitle"), ETextJustify::Center, FMargin(0, 2, 0, 12));
	int32 Best = 1;
	for (const FPortsRank& R : *State.finalScores) Best = FMath::Max(Best, R.total);
	for (const FPortsRank& R : *State.finalScores)
	{
		const FPortsPlayer& P = State.players[R.id];
		const FString Tag = P.bot ? FString::Printf(TEXT(" <small>Bot · %s</>"), *Esc(Skills.Get(P.skill).Get(TEXT("label")).AsString())) : FString();
		// .podium-row: pale, a thick bar in the house's colour, gold-edged for first place.
		FPortsBoxLook Look = PortsUi::PlainLook(Color(TEXT("#fffdf6")), 12, R.place == 1 ? Color(TEXT("#d9a82b")) : Color(TEXT("#d8bc7c")), R.place == 1 ? 3.f : 2.f);
		Look.LeftBar = Color(*P.color);
		Look.LeftBarWidth = 8;
		const FString Who = FString::Printf(TEXT("<b>%s</>%s of %s"), *Esc(P.name), *Tag, *Esc(CityName(P.home)));
		const FString Parts = FString::Printf(TEXT("Wealth %d (%dƒ, %d posts) + Family %d (%d alive) + Reputation %d (%d) + Balance %d"), R.wealth, P.florins, P.posts.Num(), R.family, Ports::FamilyTotal(P), R.reputation, P.reputation, R.balance);
		const FString Deeds = FString::Printf(TEXT("%d shipments (%d infected) · %d family lost · %d time%s fled · %d charity · %d Fortune cards · %s"),
			P.stats.shipments, P.stats.infected, P.lostFamily, P.stats.fled, P.stats.fled == 1 ? TEXT("") : TEXT("s"), P.stats.charity, P.stats.fortune,
			P.stats.protectedCount ? TEXT("protected the persecuted community") : TEXT("did not protect the persecuted community"));
		const FString Crest = P.crest, Colour = P.color;
		const int32 Place = R.place, Total = R.total;
		const FIntVector4 Bar(R.wealth, R.family, R.reputation, R.balance);
		Doc.AddBuilt([Look, Who, Parts, Deeds, Crest, Colour, Place, Total, Bar, Best](float W)
		{
			// The place and the total keep fixed rooms at the two ends.
			const float TotalWidth = 96.f;
			const float TextWidth = W > 0.f ? FMath::Max(120.f, W - 28.f - 48.f - 46.f - 16.f - TotalWidth) : 0.f;
			return PortsUi::Box(Look, SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(48).HAlign(HAlign_Center)[ Rich(FString::FromInt(Place), TEXT("Ports.Place"), ETextJustify::Center, false) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 12, 0)[ PortsUi::Crest(Colour, Crest, 30) ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Who, TEXT("Ports.Body"), ETextJustify::Left, true, TextWidth) ]
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Parts, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 3)
					[
						// .mini-bar: the four parts of the Legacy as one thin bar, as long as the score is beside the highest.
						PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#efe2bf")), 4), SNew(SBox).HeightOverride(8)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.0001f, static_cast<float>(Bar.X)))[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#d9a82b")), 0), SNew(SSpacer)) ]
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.0001f, static_cast<float>(Bar.Y)))[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#2f8f68")), 0), SNew(SSpacer)) ]
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.0001f, static_cast<float>(Bar.Z)))[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#3a73c0")), 0), SNew(SSpacer)) ]
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.0001f, static_cast<float>(Bar.W)))[ PortsUi::Box(PortsUi::PlainLook(Color(TEXT("#8a52b8")), 0), SNew(SSpacer)) ]
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.0001f, static_cast<float>(Best - Bar.X - Bar.Y - Bar.Z - Bar.W)))[ SNew(SSpacer) ]
						])
					]
					+ SVerticalBox::Slot().AutoHeight()[ Rich(Deeds, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 4, 0)[ SNew(SBox).WidthOverride(TotalWidth).HAlign(HAlign_Right)[ Rich(FString::FromInt(Total), TEXT("Ports.Total"), ETextJustify::Right, false) ] ], FMargin(16, 9, 12, 9));
		}, FMargin(0, 5));
	}
	Doc.Space(8);
	FPortsDoc Yours;
	const auto EndCard = [&Yours](const FString& Markup)
	{
		FPortsBoxLook Look = PortsUi::PlainLook(Color(TEXT("#fff8e8")), 10, Color(TEXT("#d8bc7c")), 1);
		Look.LeftBar = Color(TEXT("#b0261a"));
		Look.LeftBarWidth = 5;
		FPortsDoc Words;
		Words.Text(Markup, TEXT("Ports.Body"), ETextJustify::Left, FMargin(0));
		Yours.Boxed(Look, Words, FMargin(18, 9, 13, 9), FMargin(0, 5));
	};
	EndCard(FString::Printf(TEXT("Your houses lost <b>%d</> of %d family members (%d%%). Historians estimate that between a third and 60 percent of Europeans died."), Lost, TotalStart, FMath::RoundToInt32(100.0 * Lost / TotalStart)));
	EndCard(FString::Printf(TEXT("Your ships carried infected cargo <b>%d</> time%s, bringing the plague early to %s. In real history, trade routes carried the plague across Europe."), Infected, Infected == 1 ? TEXT("") : TEXT("s"), Early.Num() ? *FString::Join(Early, TEXT(", ")) : TEXT("no city")));
	EndCard(FString::Printf(TEXT("%s In 1349, the people who tried were overruled; the accusations were false and the violence unjust."), Protectors.Num() ? *FString::Printf(TEXT("%s took a stand to protect a persecuted community."), *FString::Join(Protectors, TEXT(", "))) : TEXT("No house took a stand to protect the persecuted community.")));
	FPortsDoc Real;
	Real.Note(Data().Timeline().Get(TEXT("epilogue")).Get(TEXT("factIds")).ToStrings(), TEXT("The real history"));
	FPortsDoc Left, Right;
	Left.Panel(TEXT("Your game"), Color(TEXT("#7a1410")), Yours, FMargin(0));
	Right.Panel(TEXT("What Really Happened"), Color(TEXT("#7a1410")), Real, FMargin(0));
	Doc.Columns({ Left, Right }, 16);
	Doc.Space(10);
	Doc.Buttons({
		PortsUi::Button(TEXT("\u21BA Watch the finale again"), [this]() { bResults = false; ShowFinale(); }),
		PortsUi::Button(FString::Printf(TEXT("Historian's Journal (%d facts)"), State.journal.Num()), [this]() { ShowJournal(); }),
		PortsUi::Button(TEXT("Main menu"), [this]() { ShowMenu(); }),
		PortsUi::Button(TEXT("Play again"), [this]() { ShowSetup(); }, EButton::Primary),
	});
	Root->SetScreen(Page(Doc.Build(1100 - 58), 1100, TEXT("bg_wood"), true, Root));
}

// ---------- Rules, Journal, city, credits ----------

void UPortsGameFlow::ShowRules()
{
	if (!FPortsData::EnsureLoaded()) return;
	const V& Book = Data().Files[TEXT("rulebook")];
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	Doc->Text(Esc(Book.Get(TEXT("title")).AsString()), TEXT("Ports.TitleSmall"), ETextJustify::Center, FMargin(0));
	Doc->Text(FString::Printf(TEXT("%s \u2014 Rule Book"), *Esc(Book.Get(TEXT("subtitle")).AsString())), TEXT("Ports.Subtitle"), ETextJustify::Center, FMargin(0, 2, 0, 10));
	Doc->Small(TEXT("These are the same rules as the printable Rule Book (both are generated from one file). Numbers in brackets like [TR-02] are historical facts; open the Historian's Journal to read them."));
	for (const V& Section : Book.Get(TEXT("sections")).GetItems())
	{
		Doc->H2(Esc(Section.Get(TEXT("title")).AsString()));
		for (const V& B : Section.Get(TEXT("blocks")).GetItems())
		{
			const FString Type = B.Get(TEXT("type")).AsString();
			if (Type == TEXT("p")) Doc->P(RuleText(B.Get(TEXT("text")).AsString()));
			else if (Type == TEXT("note")) Doc->P(FString::Printf(TEXT("<i>%s</>"), *RuleText(B.Get(TEXT("text")).AsString()).Replace(TEXT("<i>"), TEXT("<b>"))));
			else if (Type == TEXT("list"))
			{
				TArray<FString> Items;
				for (const V& Item : B.Get(TEXT("items")).GetItems()) Items.Add(RuleText(Item.AsString()));
				Doc->Bullets(Items, B.Get(TEXT("ordered")).Truthy());
			}
			else if (Type == TEXT("table"))
			{
				// Each column's share of the page is known before anything is drawn, so no cell re-wraps afterwards.
				TArray<TArray<FString>> Table;
				TArray<FString> Head;
				for (const V& Cell : B.Get(TEXT("head")).GetItems()) Head.Add(FString::Printf(TEXT("<caps>%s</>"), *RuleText(Cell.AsString())));
				Table.Add(Head);
				for (const V& RowCells : B.Get(TEXT("rows")).GetItems())
				{
					TArray<FString> Line;
					for (int32 c = 0; c < RowCells.Num(); c++) Line.Add(c == 0 ? FString::Printf(TEXT("<b>%s</>"), *RuleText(RowCells[c].AsString())) : RuleText(RowCells[c].AsString()));
					Table.Add(Line);
				}
				Doc->AddBuilt([Table](float W)
				{
					const TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
					const int32 Columns = Table[0].Num();
					const float Shares = 0.8f + FMath::Max(0, Columns - 1);
					for (int32 c = 0; c < Columns; c++) Grid->SetColumnFill(c, c == 0 ? 0.8f : 1.f);
					for (int32 r = 0; r < Table.Num(); r++) for (int32 c = 0; c < Table[r].Num() && c < Columns; c++)
					{
						const float CellWidth = W > 0.f ? FMath::Max(40.f, W * (c == 0 ? 0.8f : 1.f) / Shares - 2.f - 16.f - 2.f) : 0.f;
						const FPortsBoxLook Look = r == 0 ? PortsUi::PlainLook(PortsUi::Color(TEXT("#ecd9aa")), 0, PortsUi::Color(TEXT("#d8bc7c")), 1) : PortsUi::PlainLook(FLinearColor(1, 1, 1, 0.25f), 0, PortsUi::Color(TEXT("#d8bc7c")), 1);
						Grid->AddSlot(c, r).Padding(1)[ PortsUi::Box(Look, Rich(Table[r][c], TEXT("Ports.Body"), ETextJustify::Left, true, CellWidth), FMargin(8, 5)) ];
					}
					return Grid;
				});
			}
			else if (Type == TEXT("defs"))
			{
				for (const V& Item : B.Get(TEXT("items")).GetItems()) Doc->P(FString::Printf(TEXT("<b>%s</>  %s"), *RuleText(Item[0].AsString()), *RuleText(Item[1].AsString())));
			}
		}
	}
	FPortsDialogOptions Opts;
	Opts.bWide = true;
	Opts.EnterValue = TEXT("close");
	Open([Doc, Opts](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(TEXT("Close rules  <lsmall>Esc</>"), [Close]() { Close(TEXT("close")); }, EButton::Primary) });
		return Doc->Build(Opts.InnerWidth());
	}, Opts, nullptr);
}

void UPortsGameFlow::ShowJournal()
{
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	Doc->H2(TEXT("The Historian's Journal"));
	Doc->P(TEXT("Every historical fact you meet during play is recorded here with its source. Facts marked “Historians disagree” are still debated."));
	Doc->P(FString::Printf(TEXT("You have met <b>%d</> of the %d historical facts in the game."), State.journal.Num(), Data().Facts().Num()));
	const V& Categories = Data().Files[TEXT("facts")].Get(TEXT("categories"));
	for (int32 k = 0; k < Categories.GetKeys().Num(); k++)
	{
		TArray<FString> Ids;
		for (const FString& Id : State.journal)
		{
			const V* Fact = Data().Facts().GetItems().FindByPredicate([&Id](const V& F) { return F.Get(TEXT("id")).AsString() == Id; });
			if (Fact && Fact->Get(TEXT("category")).AsString() == Categories.GetKeys()[k]) Ids.Add(Id);
		}
		Doc->H3(FString::Printf(TEXT("%s (%d)"), *Esc(Categories.ValueAt(k).AsString()), Ids.Num()));
		if (Ids.Num() == 0) Doc->P(TEXT("<i>Not yet discovered.</>"));
		for (const FString& Id : Ids) Doc->Fact(Id);
	}
	FPortsDialogOptions Opts;
	Opts.bWide = true;
	Opts.EnterValue = TEXT("close");
	Open([Doc, Opts](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(TEXT("Close journal"), [Close]() { Close(TEXT("close")); }, EButton::Primary) });
		return Doc->Build(Opts.InnerWidth());
	}, Opts, nullptr);
}

void UPortsGameFlow::ShowCity(const FString& CityId)
{
	const FPortsCity* City = Data().FindCity(CityId);
	const FPortsCityState* C = State.City(CityId);
	if (!City || !C) return;
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	Doc->H2(Esc(City->Name));
	Doc->P(FString::Printf(TEXT("<i>%s · %s</>"), *Esc(City->Modern), *Esc(City->Region)));
	const FString Status = C->state == TEXT("stricken")
		? FString::Printf(TEXT("<risk>Stricken</>, severity %d (%s)%s"), C->severity, *Ports::SeverityName(C->severity), C->early ? TEXT(", brought early by infected cargo") : TEXT(""))
		: C->state == TEXT("aftermath") ? FString(TEXT("<b>Aftermath</>: workers scarce, prices high (shipping here earns a bonus; shipping from here costs a wage)"))
		: Ports::IsThreatened(State, CityId) ? FString(TEXT("<warn>Safe, but threatened</>: a neighbouring city is Stricken")) : FString(TEXT("<b>Safe</>"));
	Doc->P(FString::Printf(TEXT("Status: %s%s"), *Status, C->unrest ? *FString::Printf(TEXT(" · <b>Unrest</> (%d more round%s)"), C->unrest, C->unrest > 1 ? TEXT("s") : TEXT("")) : TEXT("")));
	Doc->P(FString::Printf(TEXT("When the plague really arrived: <b>%s</>%s"), *Esc(City->ArrivalDateText),
		City->ArrivalRound == 0 ? TEXT("") : *FString::Printf(TEXT(" (round: %s)"), *Esc(Ports::HalfInfo(City->ArrivalRound).Get(TEXT("label")).AsString()))));
	TArray<FString> Houses;
	for (const FPortsPlayer& P : State.players)
	{
		if (!P.posts.Contains(CityId)) continue;
		const int32 Family = Ports::FamilyAt(P, CityId);
		Houses.Add(FString::Printf(TEXT("%s %s%s"), *PortsUi::CrestGlyph(P.crest), *Esc(P.name), Family ? *FString::Printf(TEXT(" (%d family)"), Family) : TEXT("")));
	}
	Doc->P(Houses.Num() ? FString::Printf(TEXT("Trading posts: %s"), *FString::Join(Houses, TEXT(" · "))) : FString(TEXT("No house has a trading post here yet.")));
	Doc->H3(TEXT("Routes"));
	TArray<FString> Routes;
	for (const int32 Index : Data().RoutesFrom(CityId))
	{
		const FPortsRoute& R = Data().Routes[Index];
		Routes.Add(FString::Printf(TEXT("%s route to %s (value %d)"), R.bSea ? TEXT("Sea") : TEXT("Land"), *Esc(CityName(Ports::OtherEnd(R, CityId))), R.Value));
	}
	Doc->Bullets(Routes);
	TArray<FString> Facts = City->ArrivalFactIds.ToStrings();
	Facts.Append(City->Raw.Get(TEXT("factIds")).ToStrings());
	Doc->Note(Facts, FString::Printf(TEXT("History of %s"), *City->Name));
	FPortsDialogOptions Opts;
	Opts.EnterValue = TEXT("close");
	Open([Doc, Opts](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(TEXT("Close"), [Close]() { Close(TEXT("close")); }, EButton::Primary) });
		return Doc->Build(Opts.InnerWidth());
	}, Opts, nullptr);
}

void UPortsGameFlow::ShowCredits()
{
	if (!FPortsData::EnsureLoaded()) return;
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	Doc->H2(TEXT("About Ports of Plague"));
	Doc->P(TEXT("An original educational game about the spread and effects of the Black Death, 1347–1353, made by Carter K, Landon S, Valen H and John-Paul T for a high-school history class."));
	Doc->H3(TEXT("Credits"));
	Doc->Bullets({
		FString::Printf(TEXT("<b>History:</> %d facts from %d sources (see the Historian's Journal and the Research Sheet)."), Data().Facts().Num(), Data().Files[TEXT("sources")].Get(TEXT("sources")).Num()),
		TEXT("<b>Map:</> coastlines, rivers and lakes from Natural Earth (public domain)."),
		TEXT("<b>Fonts:</> EB Garamond, Cinzel and UnifrakturMaguntia, all under the SIL Open Font License."),
		TEXT("<b>Sound effects:</> original, made by the game's own code; no outside recordings are used."),
		TEXT("<b>Dice:</> the 3D dice are adapted from roll-a-die (© 2015 ukatama), under the MIT License."),
		TEXT("<b>Playing on several devices:</> the big screen and the players' phones, tablets or computers talk through Supabase Realtime. Only house names, home cities and game moves are sent; nothing is stored and there are no accounts."),
	});
	Doc->H3(TEXT("Music"));
	TArray<FString> Tracks;
	for (const V& T : Data().Files[TEXT("music")].Get(TEXT("tracks")).GetItems())
	{
		const FString Plays = T.Get(TEXT("plays")).AsString();
		Tracks.Add(FString::Printf(TEXT("<b>“%s”</> <small>— %s</>\n%s (%s) · Licensed under %s"), *Esc(T.Get(TEXT("title")).AsString()), *Esc(Plays.Left(1).ToLower() + Plays.Mid(1)),
			*Esc(T.Get(TEXT("author")).AsString()), *Esc(T.Get(TEXT("site")).AsString()), *Esc(T.Get(TEXT("license")).AsString())));
	}
	Doc->Bullets(Tracks);
	Doc->H3(TEXT("Content note"));
	Doc->P(TEXT("The game deals with mass death and with the persecution of Jewish communities. It treats these seriously and without graphic detail, and it states plainly that the accusations against Jews were false and the violence unjust."));
	Doc->H3(TEXT("License"));
	Doc->P(TEXT("This work is open source and protected under the MIT License. Copyright © 2026 Carter K, Landon S, Valen H, and John-Paul T."));
	// The notices Epic's licence asks for, word for word, at the very end.
	Doc->H3(TEXT("Engine"));
	Doc->P(TEXT("Ports of Plague uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere."));
	Doc->P(TEXT("Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved."));
	FPortsDialogOptions Opts;
	Opts.EnterValue = TEXT("close");
	Open([Doc, Opts](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(TEXT("Close"), [Close]() { Close(TEXT("close")); }, EButton::Primary) });
		return Doc->Build(Opts.InnerWidth());
	}, Opts, nullptr);
}

// ---------- Checking the game from the command line ----------
// Spec: "menu", "setup", or "mode,houses,difficulty[,auto][,nopre][,timer][,solo][,prompt:ship][,hold:chronicle]".
// With "hold:kind" a self-playing game stops at the first card of that kind and leaves it open.
// With "prompt:x" the opening cards close by themselves and that action picker opens on the first turn.
// With "auto" every house is played by the computer and cards close by themselves.
void UPortsGameFlow::StartTestGame(const FString& Spec)
{
	if (Spec == TEXT("credits")) { ShowCredits(); bTestScrollEnd = true; return; }
	if (Spec != TEXT("menu") && Spec != TEXT("credits")) MenuDueIn = 0;
	if (Spec == TEXT("continue")) { ContinueSaved(); return; }
	if (Spec == TEXT("setup")) { ShowSetup(); return; }
	// "lobbyplay": start as soon as one device has joined; "lobbyplay2": wait for two; "...timer": with the turn timer on.
	if (Spec.StartsWith(TEXT("lobby")))
	{
		ShowSetup();
		SetupForm->bDevices = true;
		bTestLobbyPlay = Spec.StartsWith(TEXT("lobbyplay"));
		TestLobbySpec = Spec;
		TestLobbyHumans = 1;
		for (int32 N = 2; N <= 6; N++) if (Spec.Contains(FString::FromInt(N))) TestLobbyHumans = N;
		bTestLobbyTimer = Spec.Contains(TEXT("timer"));
		OpenLobbyRoom();
		ShowSetup();
		return;
	}
	// "setupflip": the picture catches the first frame after a change; "setup3": the same screen at rest.
	if (Spec == TEXT("setupflip")) { ShowSetup(); bTestFlip = true; return; }
	if (Spec == TEXT("setup3")) { ShowSetup(); SetupForm->count = 3; SetupForm->players[1].bot = true; ShowSetup(); return; }
	TArray<FString> Parts;
	Spec.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() < 3) return;
	FPortsSetup Setup;
	Setup.mode = Parts[0];
	Setup.difficulty = Parts[2];
	Setup.prePlague = !Parts.Contains(TEXT("nopre"));
	Setup.timer = Parts.Contains(TEXT("timer"));
	Setup.seed = TEXT("test-") + Spec;
	const int32 Count = FMath::Clamp(FCString::Atoi(*Parts[1]), 2, 6);
	static const TCHAR* Names[] = { TEXT("House of the Anchor"), TEXT("House of the Lion"), TEXT("House of the Rose"), TEXT("House of the Star"), TEXT("House of the Ship"), TEXT("House of the Sun") };
	static const TCHAR* Homes[] = { TEXT("genoa"), TEXT("bruges"), TEXT("venice"), TEXT("london"), TEXT("florence"), TEXT("lubeck") };
	static const TCHAR* Skills[] = { TEXT("easy"), TEXT("medium"), TEXT("hard") };
	for (int32 i = 0; i < Count; i++)
	{
		FPortsSetupPlayer P;
		P.name = Names[i];
		P.home = Homes[i];
		P.bot = i > 0 && (Parts.Contains(TEXT("solo")) || i % 2 == 1);
		P.skill = Skills[i % 3];
		Setup.players.Add(P);
	}
	bAutoPlay = Parts.Contains(TEXT("auto"));
	bTestLegend = Parts.Contains(TEXT("legend"));
	bTestEarly = Parts.Contains(TEXT("fakeearly"));
	for (const FString& Part : Parts) if (Part.StartsWith(TEXT("at:"))) TestFinaleAt = FCString::Atod(*Part.Mid(3));
	for (const FString& Part : Parts) if (Part.StartsWith(TEXT("prompt:"))) TestPrompt = Part.Mid(7);
	for (const FString& Part : Parts) if (Part.StartsWith(TEXT("hold:"))) TestHold = Part.Mid(5);
	for (const FString& Part : Parts) if (Part.StartsWith(TEXT("finale:"))) TestFinale = FCString::Atoi(*Part.Mid(7));
	bTestReload = Parts.Contains(TEXT("reload"));
	TestSetup = MakeShared<FPortsSetup>(Setup);
	BeginGame(Setup, true);
}

void UPortsGameFlow::TestBeforeShot()
{
	// For a picture of the foot of a long card.
	if (bTestScrollEnd && Root.IsValid()) Root->ScrollTopToEnd();
	if (!bTestFlip || !SetupForm.IsValid()) return;
	SetupForm->count = 3;
	SetupForm->players[1].bot = true;
	ShowSetup();
}
