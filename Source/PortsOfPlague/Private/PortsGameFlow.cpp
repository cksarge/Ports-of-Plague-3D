#include "PortsGameFlow.h"
#include "PortsSettings.h"

#include "PortsSplash.h"
#include "Misc/ConfigCacheIni.h"

#include "PortsCameraPawn.h"
#include "PortsMapActor.h"
#include "PortsMapSpace.h"
#include "PortsNet.h"
#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Layout/SBox.h"

using V = FPortsValue;
using PortsUi::Esc;

namespace
{
	int32 Cfg(const TCHAR* Path) { return FPortsData::Get().Int(Path); }

	FString CityName(const FString& Id)
	{
		const FPortsCity* City = FPortsData::Get().FindCity(Id);
		return City ? City->Name : Id;
	}

	bool IsOneOf(const FString& Type, std::initializer_list<const TCHAR*> Types)
	{
		for (const TCHAR* T : Types) if (Type == T) return true;
		return false;
	}
}

double UPortsGameFlow::Now() const
{
	return Map && Map->GetWorld() ? Map->GetWorld()->GetTimeSeconds() : 0.0;
}

void UPortsGameFlow::Start(APortsMapActor* InMap)
{
	Map = InMap;
	PortsUi::Init();
	UGameViewportClient* Viewport = Map && Map->GetWorld() ? Map->GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport) return;
	Root = SNew(SPortsRoot).Map(Map);
	Viewport->AddViewportWidgetContent(Root.ToSharedRef(), 0);
	LoadAudio();
	// Every button clicks (main.js).
	PortsUi::SetClickSound([this]() { Sound(TEXT("click")); });
	PortsUi::SetDiceSound([this]() { Sound(TEXT("dice")); });
	// The menu is drawn a few frames in, once the window knows how sharp its screen is: drawn at once, its text is
	// measured for an ordinary screen and shifts a little when the Retina measurements arrive.
	MenuDueIn = 3;
	// Until then the screen is dark, as the splash screen will be, so the game opens from black and not with a
	// glimpse of the map.
	const FString Line = FCommandLine::Get();
	const bool bChecking = (Line.Contains(TEXT("PortsTest=")) || Line.Contains(TEXT("PortsPress="))) && !Line.Contains(TEXT("PortsSplash=")) && !FParse::Param(*Line, TEXT("PortsStart"));
	if (!bChecking) Root->SetScreen(SNew(SPortsSplash).Still(true));
}

// Which splash screen opens the game: "official" or none ([PortsOfPlague.Splash] Style in DefaultGame.ini;
// -PortsSplash=... on the command line says otherwise). Games started for checking go straight in.
FString UPortsGameFlow::SplashStyle() const
{
	FString Style;
	if (FParse::Value(FCommandLine::Get(), TEXT("PortsSplash="), Style)) return Style.ToLower();
	if (FString(FCommandLine::Get()).Contains(TEXT("PortsTest=")) || FString(FCommandLine::Get()).Contains(TEXT("PortsPress="))) return FString();
	GConfig->GetString(TEXT("PortsOfPlague.Splash"), TEXT("Style"), Style, GGameIni);
	return Style.ToLower();
}

// How out of focus the map behind the start screen is: fully, until Start is pressed, then easing to sharp.
float UPortsGameFlow::StartBlur() const
{
	constexpr float Full = 14.f;
	if (StartLeftAt <= 0) return bStartScreen ? Full : 0.f;
	return Full * (1.f - FMath::SmoothStep(0.f, 1.f, static_cast<float>((FPlatformTime::Seconds() - StartLeftAt) / StartClearSeconds)));
}

void UPortsGameFlow::Stop()
{
	// Closing the game tells every device the big screen has gone.
	if (LobbyRoom.IsValid()) LobbyRoom->Close();
	if (Room.IsValid()) Room->Close();
	LobbyRoom.Reset();
	Room.Reset();
	if (Root.IsValid())
	{
		if (UGameViewportClient* Viewport = Map && Map->GetWorld() ? Map->GetWorld()->GetGameViewport() : nullptr) Viewport->RemoveViewportWidgetContent(Root.ToSharedRef());
		Root.Reset();
	}
	Steps.Reset();
	PromptRebuild.Reset();
	PromptCityChosen = nullptr;
}

bool UPortsGameFlow::IsPointerOverUi() const
{
	return Root.IsValid() && Root->IsPointerOverUi();
}

APortsCameraPawn* UPortsGameFlow::Camera() const
{
	APlayerController* PC = Map && Map->GetWorld() ? Map->GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<APortsCameraPawn>(PC->GetPawn()) : nullptr;
}

void UPortsGameFlow::LookAt(const TArray<FString>& CityIds, double MaxZoomIn)
{
	APortsCameraPawn* Pawn = Camera();
	if (!Pawn || bAutoPlay) return;
	TArray<FVector2D> Pixels;
	for (const FString& Id : CityIds) if (const FPortsCity* City = FPortsData::Get().FindCity(Id)) Pixels.Add(PortsMapSpace::CityPixel(*City));
	Pawn->Frame(Pixels, MaxZoomIn);
}

void UPortsGameFlow::LookAtWholeMap()
{
	if (APortsCameraPawn* Pawn = Camera()) Pawn->FrameWholeMap();
}

void UPortsGameFlow::HoldFor(double Seconds)
{
	if (!bAutoPlay) HoldUntil = Now() + Seconds;
}

bool UPortsGameFlow::OnePerson() const
{
	int32 People = 0;
	for (const FPortsPlayer& P : State.players) if (!P.bot) People++;
	return People <= 1;
}

bool UPortsGameFlow::Busy() const
{
	return !Root.IsValid() || Root->HasDialog() || (Map && Map->IsAnimating()) || Steps.Num() > 0 || bBotWaiting || HoldUntil > 0;
}

void UPortsGameFlow::Notify(const FString& Text, float Seconds)
{
	if (Root.IsValid() && !Text.IsEmpty()) Root->Toast(Text, Seconds);
	// On several devices the message also goes to the device of the house whose turn it is.
	const FPortsPlayer* P = bInGame && Room.IsValid() ? Ports::CurrentPlayer(State) : nullptr;
	if (P && !P->bot && !Text.IsEmpty()) Room->Toast(P->id, Text);
}

// ---------- Saving ----------
// One saved game, written after every change, in the same shape as the web
// version's save: { savedAt, state, ui }.

FString UPortsGameFlow::SavePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("ports-of-plague-save.json"));
}

void UPortsGameFlow::Save()
{
	if (bAutoPlay && !bTestReload) return;
	const V Out = V::Object({
		{ TEXT("savedAt"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0 },
		{ TEXT("state"), State.ToValue() },
		{ TEXT("ui"), V::Object({ { TEXT("hints"), Ui.hints }, { TEXT("history"), Ui.history }, { TEXT("tutorial"), Ui.tutorial }, { TEXT("lessons"), V::Strings(Ui.lessons) }, { TEXT("seenSeq"), Ui.seenSeq }, { TEXT("lastNote"), V::Strings(Ui.lastNote) },
			{ TEXT("room"), Room.IsValid() ? V::Object({ { TEXT("code"), V(Room->Code) }, { TEXT("seats"), Room->SavedSeatsValue() } }) : V::Null() } }) },
	});
	FFileHelper::SaveStringToFile(Out.ToJson(), *SavePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	PushToDevices();
}

bool UPortsGameFlow::LoadSaved(FPortsState& OutState, FPortsUiState& OutUi) const
{
	FString Text;
	V In;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()) || !V::Parse(Text, In)) return false;
	if (In.Get(TEXT("state")).Get(TEXT("version")).AsInt() != Ports::SAVE_VERSION || In.Get(TEXT("state")).Get(TEXT("phase")).AsString() == TEXT("ended")) return false;
	if (!FPortsState::FromValue(In.Get(TEXT("state")), OutState)) return false;
	const V& U = In.Get(TEXT("ui"));
	OutUi.hints = U.Get(TEXT("hints")).Truthy();
	// Games saved before History Mode existed have it on.
	OutUi.history = !U.Has(TEXT("history")) || U.Get(TEXT("history")).Truthy();
	OutUi.tutorial = U.Get(TEXT("tutorial")).Truthy();
	OutUi.lessons = U.Get(TEXT("lessons")).ToStrings();
	OutUi.seenSeq = U.Get(TEXT("seenSeq")).AsInt();
	OutUi.lastNote = U.Get(TEXT("lastNote")).ToStrings();
	OutUi.roomCode = U.Get(TEXT("room")).Get(TEXT("code")).AsString();
	OutUi.roomSeats = U.Get(TEXT("room")).Get(TEXT("seats"));
	return true;
}

// When a self-playing game ends: writes its result to the log, and says whether the rules engine, playing the
// same game from the same setup with no screens at all, ends in exactly the same state.
void UPortsGameFlow::TestCheckResult()
{
	if (!bAutoPlay || !TestSetup.IsValid() || !State.finalScores.IsSet()) return;
	FString Scores;
	for (const FPortsRank& R : *State.finalScores) Scores += FString::Printf(TEXT("%s%d"), Scores.IsEmpty() ? TEXT("") : TEXT(","), R.total);
	FPortsState Alone;
	const int32 Turns = Ports::PlayBotGame(*TestSetup, Alone);
	const bool bSame = Turns >= 0 && Alone.ToJson() == State.ToJson();
	UE_LOG(LogTemp, Display, TEXT("PortsResult: %s houses=%d rounds=%d scores=%s engine=%s"), *TestSetup->mode, State.players.Num(), Ports::TotalRounds(State), *Scores, bSame ? TEXT("SAME") : TEXT("DIFFERENT"));
	TestSetup.Reset();
	if (FParse::Param(FCommandLine::Get(), TEXT("PortsQuitAtEnd"))) FPlatformMisc::RequestExit(false);
}

bool UPortsGameFlow::HasLeft(const FPortsPlayer* Player) const
{
	return Room.IsValid() && Player && Room->Seats.IsValidIndex(Player->id) && Room->Seats[Player->id].left;
}

V UPortsGameFlow::DeviceView() const
{
	// The story card waiting for Next (if any), whether the big screen is busy, and the turn clock.
	const V Next = StoryId ? V::Object({ { TEXT("id"), StoryId }, { TEXT("label"), V(StoryLabel) }, { TEXT("title"), V(StoryTitle) }, { TEXT("kind"), V(StoryKind) }, { TEXT("data"), StoryData } }) : V::Null();
	// (A tutorial lesson has no kind and no data: a device shows its title and the button, and the card is read on this screen.)
	const bool bActing = (Map && Map->IsAnimating()) || Steps.Num() > 0 || HoldUntil > 0;
	const bool bHold = Root.IsValid() && (Root->HasStoryDialog() || bActing);
	const V Timer = bClockOn ? V::Object({ { TEXT("left"), FMath::RoundToDouble(FMath::Max(0.0, ClockLeft) * 10.0) / 10.0 }, { TEXT("running"), !bHold } }) : V::Null();
	return V::Object({ { TEXT("next"), Next }, { TEXT("busy"), bActing }, { TEXT("hints"), Ui.hints }, { TEXT("note"), V::Strings(Ui.lastNote) }, { TEXT("timer"), Timer }, { TEXT("history"), Ui.history } });
}

void UPortsGameFlow::PushToDevices()
{
	if (Room.IsValid() && PushAt <= 0) PushAt = FPlatformTime::Seconds() + 0.06;
}

// handleRequest in game.js: the checks first, then the same steps a click on this screen would take.
void UPortsGameFlow::HandleIntent(const V& M)
{
	if (!Room.IsValid() || !bInGame) return;
	const FString From = M.Get(TEXT("from")).AsString(), T = M.Get(TEXT("t")).AsString();
	const int32 Seat = Room->Seats.IndexOfByPredicate([&From](const FPortsSeat& S) { return S.cid == From; });
	const FString Problem = PortsNet::ValidateIntent(State, Room->Seats, M);
	if (!Problem.IsEmpty()) Room->Toast(Seat, Problem);
	else if (T == TEXT("next"))
	{
		if (StoryId && M.Get(TEXT("id")).AsInt() == StoryId) Root->CloseTop(TEXT("ok"));
	}
	else if (Busy()) Room->Toast(Seat, TEXT("Please wait…"));
	else if (T == TEXT("act")) RunAction(FPortsAction::FromValue(M.Get(TEXT("action"))));
	else if (T == TEXT("decide"))
	{
		const V& C = M.Get(TEXT("choice"));
		ApplyDecision(C.IsString() ? (C.AsString() == TEXT("obey") ? EPortsChoice::Obey : EPortsChoice::Pay) : C.AsBool() ? EPortsChoice::Yes : EPortsChoice::No);
		Pump();
	}
	else if (T == TEXT("end")) FinishTurn();
	Room->Handled(M);
	Refresh();
	PushToDevices();
}

void UPortsGameFlow::ClearSave()
{
	IFileManager::Get().Delete(*SavePath());
}

// ---------- The sequence of play ----------

void UPortsGameFlow::MarkSeen(int32 Seq)
{
	Ui.seenSeq = FMath::Max(Ui.seenSeq, Seq);
	Save();
}

void UPortsGameFlow::SetNote(const V& FactIds)
{
	if (FactIds.Num() == 0 || !Ui.history) return;
	Ui.lastNote.Reset();
	for (const FString& Id : FactIds.ToStrings())
	{
		Ui.lastNote.AddUnique(Id);
		if (Ui.lastNote.Num() == 3) break;
	}
}

// Moves the game on as far as it can go without a person: shows the next card,
// advances the phase, starts the next turn. It stops whenever a card or choice
// is open, a ship is sailing or a bot is thinking, and is called again when
// that is over.
void UPortsGameFlow::Pump()
{
	if (!bInGame || !Root.IsValid()) return;
	if (bPumping) { bPumpAgain = true; return; }
	TGuardValue<bool> Guard(bPumping, true);
	for (int32 Safety = 0; Safety < 10000; Safety++)
	{
		if (!bInGame) return;
		if (Root->HasDialog() || Map->IsAnimating() || bBotWaiting || HoldUntil > 0) return;
		if (Steps.Num())
		{
			const TFunction<void()> Step = Steps[0];
			Steps.RemoveAt(0);
			Step();
			continue;
		}
		if (TutorialBefore()) continue;
		if (PresentNew()) continue;
		if (State.phase == TEXT("ended") && Lesson(TEXT("final"))) continue;
		// The game is over: the finale plays, then the results (straight to the results when the game is only checking itself).
		if (State.phase == TEXT("ended") && Room.IsValid()) { PushAt = 0; Room->PushState(State.ToValue(), DeviceView()); }
		if (State.phase == TEXT("ended")) { if (bAutoPlay && TestFinale < 0) ShowEnd(); else ShowFinale(); return; }
		if (State.phase != TEXT("actions"))
		{
			Ports::Advance(State);
			Save();
			Refresh();
			continue;
		}
		const FPortsPlayer* P = Ports::CurrentPlayer(State);
		if (!P) return;
		const int32 TurnKey = State.round * 100 + State.turn + 1000;
		if (HandledTurn != TurnKey)
		{
			HandledTurn = TurnKey;
			BeginTurn();
			continue;
		}
		Refresh();
		if (P->bot || bAutoPlay)
		{
			// A computer house plays on screen like a person: a short pause to "think" before each move.
			bBotWaiting = true;
			BotMoveAt = Now() + (bAutoPlay ? 0.05 : FPortsData::Get().Number(TEXT("bots.thinkSeconds")));
			return;
		}
		// A house whose player has left sits out: its cards get the cautious answer (offers are turned down, a wage
		// law is obeyed) and its turn ends (skipLeftTurn in game.js).
		if (HasLeft(P))
		{
			while (P && P->pending.Num() && HasLeft(P))
			{
				const int32 Waiting = P->pending.Num();
				ApplyDecision(P->pending[0].Get(TEXT("kind")).AsString() == TEXT("wageLaw") ? EPortsChoice::Obey : EPortsChoice::No);
				P = Ports::CurrentPlayer(State);
				if (P && P->pending.Num() >= Waiting) break;
			}
			if (Steps.Num() || Root->HasDialog()) continue;
			Notify(FString::Printf(TEXT("%s has left the game: their turn is skipped."), *P->name), 3.5f);
			FinishTurn();
			return;
		}
		if (TutorialTurn(*P)) continue;
		// A card waiting for an answer: asked here, or answered on the player's own device.
		if (P->pending.Num() && !Remote()) { OpenDecisionPrompt(); return; }
		return;
	}
}

// Shows the next entries of the engine's log that the players have not seen yet,
// grouped as presentNew in game.js groups them. Returns false when all are seen.
bool UPortsGameFlow::PresentNew()
{
	const int32 Index = State.log.IndexOfByPredicate([this](const V& E) { return E.Get(TEXT("seq")).AsInt() > Ui.seenSeq; });
	if (Index == INDEX_NONE) return false;
	const V Next = State.log[Index];
	const FString Type = Next.Get(TEXT("type")).AsString();
	const auto TypeAt = [this](int32 i) { return State.log.IsValidIndex(i) ? State.log[i].Get(TEXT("type")).AsString() : FString(); };
	const auto SeqOf = [](const V& E) { return E.Get(TEXT("seq")).AsInt(); };

	if (Type == TEXT("prologue"))
	{
		Refresh();
		Sound(TEXT("bell"));
		Story(TEXT("prologue"), V::Object({ { TEXT("e"), Next } }), [this, Next, SeqOf]() { SetNote(Next.Get(TEXT("factIds"))); MarkSeen(SeqOf(Next)); });
	}
	else if (Type == TEXT("orderRoll"))
	{
		Sound(TEXT("bell"));
		Story(TEXT("order"), V::Object({ { TEXT("e"), Next } }), [this, Next, SeqOf]() { Sound(TEXT("fanfare")); MarkSeen(SeqOf(Next)); });
	}
	else if (Type == TEXT("round"))
	{
		V Group = V::Array({ Next });
		for (int32 i = Index + 1; IsOneOf(TypeAt(i), { TEXT("arrival"), TEXT("arrivalAlready") }); i++) Group.Add(State.log[i]);
		// Checking saved games: write the game to disk and carry on from what is read back.
		if (bTestReload && ReloadedRound != State.round)
		{
			ReloadedRound = State.round;
			Save();
			const FString Before = State.ToJson();
			FPortsState Read;
			FPortsUiState ReadUi;
			if (LoadSaved(Read, ReadUi) && Read.ToJson() == Before) { State = Read; Ui = ReadUi; UE_LOG(LogTemp, Display, TEXT("PortsReload: round %d saved and loaded back the same."), State.round); }
			else UE_LOG(LogTemp, Error, TEXT("PortsReload: round %d did NOT load back the same."), State.round);
			return true;
		}
		// The round's trading song starts with its opening card (darker as the years pass).
		RoundSong = GameSong();
		Refresh();
		LookAtWholeMap();
		Sound(TEXT("bell"));
		Sound(TEXT("stamp"), 0.25);
		if (Group.Num() > 1) Sound(TEXT("plague"), 0.9);
		Story(TEXT("round"), V::Object({ { TEXT("group"), Group } }), [this, Group, SeqOf]()
		{
			TArray<FString> Struck;
			for (int32 i = 1; i < Group.Num(); i++)
			{
				if (Group[i].Get(TEXT("type")).AsString() != TEXT("arrival")) continue;
				Struck.Add(Group[i].Get(TEXT("city")).AsString());
				Map->FloatText(Struck.Last(), TEXT("Plague!"), false);
			}
			SetNote(Group[0].Get(TEXT("factIds")));
			MarkSeen(SeqOf(Group[Group.Num() - 1]));
			Refresh();
			// The camera goes to where the plague has just arrived and watches the stain spread, then draws back.
			if (Struck.Num() && !bAutoPlay)
			{
				LookAt(Struck, 2.6);
				HoldFor(2.1);
				Steps.Insert([this]() { LookAtWholeMap(); }, 0);
			}
		});
	}
	else if (Type == TEXT("card") && Next.Get(TEXT("deck")).AsString() == TEXT("chronicle"))
	{
		// All of the round's Chronicle cards, a few to a page.
		V Groups = V::Array();
		V Facts = V::Array();
		int32 i = Index;
		int32 LastSeq = SeqOf(Next);
		while (TypeAt(i) == TEXT("card") && State.log[i].Get(TEXT("deck")).AsString() == TEXT("chronicle"))
		{
			V Group = V::Array({ State.log[i] });
			for (const V& Id : Ports::CardById(State.log[i].Get(TEXT("card")).AsString()).Get(TEXT("factIds")).GetItems()) Facts.Add(Id);
			LastSeq = SeqOf(State.log[i++]);
			while (TypeAt(i) == TEXT("effect")) { Group.Add(State.log[i]); LastSeq = SeqOf(State.log[i++]); }
			Groups.Add(Group);
		}
		{
			bool bPersecution = false;
			for (const V& G : Groups.GetItems()) bPersecution |= Ports::CardById(G[0].Get(TEXT("card")).AsString()).Get(TEXT("theme")).AsString() == TEXT("persecution");
			Sound(bPersecution ? TEXT("knell") : TEXT("page"));
		}
		Story(TEXT("chronicle"), V::Object({ { TEXT("groups"), Groups } }), [this, Facts, LastSeq]() { SetNote(Facts); MarkSeen(LastSeq); Refresh(); });
	}
	else if (Type == TEXT("card"))
	{
		V Group = V::Array({ Next });
		int32 LastSeq = SeqOf(Next);
		for (int32 i = Index + 1; TypeAt(i) == TEXT("effect"); i++) { Group.Add(State.log[i]); LastSeq = SeqOf(State.log[i]); }
		const V Facts = Ports::CardById(Next.Get(TEXT("card")).AsString()).Get(TEXT("factIds"));
		Sound(Ports::CardById(Next.Get(TEXT("card")).AsString()).Get(TEXT("theme")).AsString() == TEXT("persecution") ? TEXT("knell") : TEXT("page"));
		Story(TEXT("card"), V::Object({ { TEXT("group"), Group } }), [this, Facts, LastSeq]() { SetNote(Facts); MarkSeen(LastSeq); Refresh(); });
	}
	else if (Type == TEXT("plague"))
	{
		// All plague phases of this round (three in Quick Play) are shown together.
		V Group = V::Array();
		bool bNews = false, bPre = true;
		TArray<FString> Passed;
		for (int32 i = Index; IsOneOf(TypeAt(i), { TEXT("plague"), TEXT("mortality"), TEXT("aftermath"), TEXT("upkeep"), TEXT("loanRepaid"), TEXT("loanDefault"), TEXT("dealEnd"), TEXT("gatesOpen") }); i++)
		{
			const V& E = State.log[i];
			const FString T = TypeAt(i);
			if (T == TEXT("plague")) { if (!E.Get(TEXT("pre")).Truthy()) bPre = false; }
			else if (T == TEXT("aftermath")) Passed.Add(CityName(E.Get(TEXT("city")).AsString()));
			else bNews = true;
			Group.Add(E);
		}
		const int32 LastSeq = SeqOf(Group[Group.Num() - 1]);
		// Nothing for anyone to read or roll: a short message instead of a card.
		if (!bNews && State.roundEnd < Cfg(TEXT("rounds")))
		{
			Notify(bPre ? FString(TEXT("The year turns. No plague yet.")) : FString::Printf(TEXT("No family was in a Stricken city.%s"), Passed.Num() ? *FString::Printf(TEXT(" The plague passes from %s."), *FString::Join(Passed, TEXT(", "))) : TEXT("")), 3.5f);
			MarkSeen(LastSeq);
			Refresh();
		}
		else
		{
			Refresh();
			// The plague song plays only while these results are on screen: from here until the card is closed.
			bPlagueCard = !bPre;
			bool bDeaths = false;
			for (const V& E : Group.GetItems()) bDeaths |= E.Get(TEXT("deaths")).AsInt() > 0;
			if (bDeaths) Sound(TEXT("knell"), 1.0); else Sound(TEXT("low"));
			Story(TEXT("plague"), V::Object({ { TEXT("group"), Group } }), [this, LastSeq]()
			{
				bPlagueCard = false;
				RoundSong = GameSong();
				MarkSeen(LastSeq);
				Refresh();
			});
		}
	}
	else if (Type == TEXT("fortune"))
	{
		const V Facts = Ports::FortuneById(Next.Get(TEXT("card")).AsString()).Get(TEXT("factIds"));
		Sound(Ports::FortuneById(Next.Get(TEXT("card")).AsString()).Get(TEXT("tone")).AsString() == TEXT("bad") ? TEXT("misfortune") : TEXT("fortune"));
		Story(TEXT("fortune"), V::Object({ { TEXT("e"), Next } }), [this, Facts, Next, SeqOf]() { SetNote(Facts); MarkSeen(SeqOf(Next)); Refresh(); });
	}
	else
	{
		MarkSeen(SeqOf(Next));
	}
	return true;
}

// The start of a house's turn: hand the device over (unless a bot plays, or
// one person keeps the device all game), then start the clock.
void UPortsGameFlow::BeginTurn()
{
	const FPortsPlayer& P = *Ports::CurrentPlayer(State);
	BotMoves = 0;
	bClockOn = false;
	Refresh();
	{
		TArray<FString> Own = P.posts;
		Own.AddUnique(P.home);
		LookAt(Own, 1.35);
	}
	if (P.bot || bAutoPlay) return;
	if (Remote())
	{
		// Everyone has their own device: the turn simply begins.
		Sound(TEXT("fanfare"));
		StartClock();
		PushToDevices();
		return;
	}
	if (OnePerson())
	{
		Sound(TEXT("fanfare"));
		Notify(FString::Printf(TEXT("%s: your turn."), *P.name), 2.5f);
		StartClock();
		return;
	}
	const FString Name = P.name, Home = CityName(P.home), Color = P.color, Crest = P.crest;
	const bool bFavor = State.guildFavor.IsSet() && *State.guildFavor == P.id;
	const FString Label = Ports::RoundInfo(State).label;
	Steps.Add([this, Name, Home, Color, Crest, bFavor, Label]()
	{
		Sound(TEXT("fanfare"));
		FPortsDialogOptions Opts;
		Opts.bDismissable = false;
		Opts.EnterValue = TEXT("go");
		Open([Name, Home, Color, Crest, bFavor, Label](TFunction<void(const FString&)> Close)
		{
			FPortsDoc Doc;
			Doc.Add(SNew(SBox).HAlign(HAlign_Center)[ PortsUi::Banner(Crest, 150) ], FMargin(0, 6));
			Doc.H2(FString::Printf(TEXT("Pass the device to %s"), *Esc(Name)), ETextJustify::Center);
			Doc.P(FString::Printf(TEXT("%s. %s of %s: take the device, then start your turn.%s"), *Esc(Label), *Esc(Name), *Esc(Home), bFavor ? TEXT(" You receive Guild’s Favor this round: +1 action point.") : TEXT("")), ETextJustify::Center);
			Doc.Space(6);
			Doc.Add(SNew(SBox).HAlign(HAlign_Center)[ PortsUi::Button(FString::Printf(TEXT("I am %s: start my turn  <small>Enter</>"), *Esc(Name)), [Close]() { Close(TEXT("go")); }, PortsUi::EButton::Gold) ]);
			return Doc.Build(FPortsDialogOptions().InnerWidth());
		}, Opts, [this](const FString&) { StartClock(); });
	});
}

void UPortsGameFlow::StartClock()
{
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!State.turnSeconds || !P || P->bot) return; // computer houses are never timed
	bClockOn = true;
	ClockLeft = State.turnSeconds;
	ClockShown = -1;
}

// Time is up: close any choice still open, answer waiting cards with their
// cautious default (see TimeUp) and move on to the next house.
void UPortsGameFlow::ExpireTurn()
{
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	bClockOn = false;
	if (!P) return;
	const FString Name = P->name;
	Root->CloseAllSilently();
	OpenPrompt.Reset();
	Selectable.Reset();
	PromptRebuild.Reset();
	PromptCityChosen = nullptr;
	Ports::TimeUp(State);
	UE_LOG(LogTemp, Display, TEXT("PortsTimer: time ran out for %s; the turn passed on."), *Name);
	Save();
	Notify(FString::Printf(TEXT("Time is up for %s. The turn passes on."), *Name), 3.5f);
	RefreshTopBar();
	Pump();
}

void UPortsGameFlow::StartAction(const FString& Id)
{
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!bInGame || !P || Busy() || P->bot || Remote()) return;
	const FString Why = QuickBlock(Id, *P);
	if (!Why.IsEmpty()) { Sound(TEXT("error")); Notify(Why); return; }
	OpenActionPrompt(Id);
}

void UPortsGameFlow::RunAction(const FPortsAction& Action)
{
	const FString Reason = Ports::CheckAction(State, Action);
	if (!Reason.IsEmpty()) { Sound(TEXT("error")); Notify(Reason, 4.5f); Refresh(); return; }
	const FPortsResult Result = Ports::PerformAction(State, Action);
	if (!Result.ok) { Notify(Result.reason, 4.5f); Refresh(); return; }
	Save();
	ShowActionResult(Result.entry);
	const int32 Seq = Result.entry.Get(TEXT("seq")).AsInt();
	// Then any Fortune card this action drew is shown, and an offer on it is answered straight away.
	Steps.Add([this, Seq]() { MarkSeen(Seq); Refresh(); });
	Pump();
}

void UPortsGameFlow::ShowActionResult(const V& E)
{
	SetNote(E.Get(TEXT("factIds")));
	const FString Type = E.Get(TEXT("type")).AsString();
	const FPortsPlayer& P = State.players[E.Get(TEXT("player")).AsInt()];
	const FString City = E.Get(TEXT("city")).AsString();
	if (Type == TEXT("ship"))
	{
		const FLinearColor Color = PortsUi::Color(*P.color);
		Steps.Add([this, E, Color]()
		{
			Refresh();
			const bool bInfected = E.Get(TEXT("infected")).Truthy();
			if (bAutoPlay) return;
			// The camera goes to the port the ship leaves from, sails with it (see Tick), and draws back when the cargo is landed.
			LookAt({ E.Get(TEXT("from")).AsString() }, 2.6);
			const FPortsRoute* Sailed = FPortsData::Get().FindRoute(E.Get(TEXT("route")).AsString());
			Sound(Sailed && Sailed->bSea ? TEXT("sail") : TEXT("cart"));
			Map->AnimateShipment(E.Get(TEXT("route")).AsString(), E.Get(TEXT("from")).AsString(), Color, bInfected, [this, E, bInfected]()
			{
				Map->FloatText(E.Get(TEXT("to")).AsString(), FString::Printf(TEXT("+%dƒ"), E.Get(TEXT("profit")).AsInt()), true);
				if (bInfected) Map->FloatText(E.Get(TEXT("from")).AsString(), TEXT("Infected!"), false);
				Sound(bInfected ? TEXT("plague") : TEXT("coin"));
				LookAtWholeMap();
				Pump();
			});
		});
		Story(TEXT("ship"), V::Object({ { TEXT("e"), E } }));
		if (E.Get(TEXT("spread")).AsString() == TEXT("early"))
		{
			const V* Arrival = State.log.FindByPredicate([&E](const V& X) { return X.Get(TEXT("type")).AsString() == TEXT("arrival") && X.Get(TEXT("city")) == E.Get(TEXT("to")) && X.Get(TEXT("early")).Truthy(); });
			if (Arrival) Story(TEXT("spread"), V::Object({ { TEXT("arrival"), *Arrival } }));
		}
		return;
	}
	if (Type == TEXT("physician"))
	{
		Story(TEXT("physician"), V::Object({ { TEXT("e"), E } }));
		return;
	}
	if (Type == TEXT("post")) Map->FloatText(City, TEXT("New post!"), true);
	if (Type == TEXT("charity")) Map->FloatText(P.home, TEXT("+rep"), true);
	if (Type == TEXT("marry")) Map->FloatText(City, TEXT("+1 family"), true);
	if (Type == TEXT("land")) Map->FloatText(City, TEXT("Land!"), true);
	if (Type == TEXT("loan")) Map->FloatText(P.home, FString::Printf(TEXT("+%dƒ"), Cfg(TEXT("gains.loan"))), true);
	if (Type == TEXT("gates")) Map->FloatText(City, TEXT("Gates closed"), false);
	Sound(TEXT("coin"));
	Notify(E.Get(TEXT("text")).AsString(), 4.2f);
}

void UPortsGameFlow::ApplyDecision(EPortsChoice Choice)
{
	FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!P || P->pending.Num() == 0) return;
	const V D = P->pending[0];
	const FString Kind = D.Get(TEXT("kind")).AsString();
	const FPortsResult R = Ports::Decide(State, Choice);
	const EPortsChoice Fallback = Kind == TEXT("wageLaw") ? EPortsChoice::Pay : EPortsChoice::No;
	if (!R.ok && (P->bot || bAutoPlay) && Choice != Fallback) { ApplyDecision(Fallback); return; } // a bot that cannot accept declines
	if (!R.ok) { Notify(R.reason); return; }
	Save();
	MarkSeen(R.entry.Get(TEXT("seq")).AsInt());
	const FString Text = R.entry.Get(TEXT("text")).AsString();
	// A coin for anything agreed to (not for the wage law, which is no bargain).
	if (Choice != EPortsChoice::No && Kind != TEXT("wageLaw")) Sound(TEXT("coin"));
	if (Kind == TEXT("deal"))
	{
		Notify(Text, 4.5f);
		SetNote(V::Array({ V(TEXT("TR-04")) }));
	}
	else
	{
		const V& Card = Ports::CardById(D.Get(TEXT("card")).AsString());
		if (Kind == TEXT("wageLaw") && Choice == EPortsChoice::Pay) Story(TEXT("wage"), V::Object({ { TEXT("entry"), R.entry } }));
		else if (D.Get(TEXT("reveal")).Truthy() && Choice != EPortsChoice::No) Story(TEXT("reveal"), V::Object({ { TEXT("cardId"), Card.Get(TEXT("id")) }, { TEXT("entry"), R.entry } }));
		else Notify(Text, 4.f);
		SetNote(Card.Get(TEXT("factIds")));
	}
	Refresh();
}

void UPortsGameFlow::TryEndTurn()
{
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!bInGame || !P || Busy() || P->bot || Remote()) return;
	if (P->pending.Num()) { Notify(TEXT("Answer the card first.")); return; }
	if (P->ap > 0) OpenEndTurnPrompt();
	else FinishTurn();
}

void UPortsGameFlow::FinishTurn()
{
	const FPortsResult R = Ports::EndTurn(State);
	Save();
	if (!R.ok) { Notify(R.reason); return; }
	bClockOn = false;
	RefreshTopBar();
	Pump();
}

void UPortsGameFlow::BotStep()
{
	bBotWaiting = false;
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!bInGame || !P) return;
	if (++BotMoves > 40) { FinishTurn(); return; }
	const FPortsBotMove Move = Ports::BotMove(State);
	if (Move.type == FPortsBotMove::EType::End) { FinishTurn(); return; }
	if (Move.type == FPortsBotMove::EType::Act) RunAction(Move.action);
	else ApplyDecision(Move.choice);
	Pump();
}

void UPortsGameFlow::OnMapClicked(const FVector2D& Pixel)
{
	if (!Map || !Root.IsValid()) return;
	const FString City = Map->CityAt(Pixel);
	if (City.IsEmpty()) return;
	if (PromptCityChosen) { const TFunction<void(const FString&)> Chosen = PromptCityChosen; Chosen(City); return; }
	if (bInGame && !Busy()) ShowCity(City);
}

void UPortsGameFlow::HandleKeys()
{
	const APlayerController* PC = Map && Map->GetWorld() ? Map->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !Root.IsValid() || Finale.IsValid() || SplashUntil > 0) return;
	if (bStartScreen)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)) LeaveStart();
		return;
	}
	// Settings is waiting for a key to be chosen: that key does nothing else.
	if (SettingsKeyCapture()) return;
	const FPortsSettings& Keys = FPortsSettings::Get();
	if (PC->WasInputKeyJustPressed(EKeys::Escape) && Root->CancelTop()) return;
	if (PC->WasInputKeyJustPressed(EKeys::Enter) && Root->ConfirmTop()) return;
	if (Root->HasDialog()) return;
	if (PC->WasInputKeyJustPressed(Keys.Key(TEXT("rules")))) { ShowRules(); return; }
	if (!bInGame) return;
	if (PC->WasInputKeyJustPressed(Keys.Key(TEXT("journal"))) && Ui.history) { ShowJournal(); return; }
	if (PC->WasInputKeyJustPressed(Keys.Key(TEXT("sound")))) { SetSoundOn(!bSoundOn); Notify(bSoundOn ? TEXT("Sound effects on") : TEXT("Sound effects off"), 1.2f); RefreshTopBar(); return; }
	if (PC->WasInputKeyJustPressed(Keys.Key(TEXT("music")))) { SetMusicOn(!bMusicOn); Notify(bMusicOn ? TEXT("Music on") : TEXT("Music off"), 1.2f); RefreshTopBar(); return; }
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (State.phase != TEXT("actions") || Busy() || !P || P->bot) return;
	for (const V& A : FPortsData::Get().Actions().GetItems())
	{
		const FString Id = A.Get(TEXT("id")).AsString();
		if (PC->WasInputKeyJustPressed(Keys.Key(FName(*Id)))) { StartAction(Id); return; }
	}
	if (PC->WasInputKeyJustPressed(Keys.Key(TEXT("end")))) TryEndTurn();
}

void UPortsGameFlow::Tick(float DeltaSeconds)
{
	if (!Root.IsValid() || !Map) return;
	if (MenuDueIn > 0 && --MenuDueIn == 0 && !bInGame && !SetupForm.IsValid() && !Finale.IsValid())
	{
		// The game opens on its splash screen, if it has one, which fades away onto the start screen. Games and
		// screens opened for checking go straight to the menu (-PortsStart asks for the start screen all the same).
		const FString Splash = SplashStyle();
		const FString Line = FCommandLine::Get();
		const bool bChecking = (Line.Contains(TEXT("PortsTest=")) || Line.Contains(TEXT("PortsPress="))) && !Line.Contains(TEXT("PortsSplash=")) && !FParse::Param(*Line, TEXT("PortsStart"));
		if (bChecking) ShowMenu();
		else if (Splash == TEXT("official"))
		{
			SplashUntil = FPlatformTime::Seconds() + SPortsSplash::Seconds();
			ShowStart(SNew(SPortsSplash));
		}
		else ShowStart(SNew(SPortsSplash).Blank(true));
	}
	if (SplashUntil > 0 && FPlatformTime::Seconds() >= SplashUntil) SplashUntil = 0;
	// Start has been pressed. The title fades first; the menu's card rises while the map is still coming into
	// focus, and the blur goes on clearing behind it, so nothing changes all at once.
	if (StartLeftAt > 0)
	{
		const double Since = FPlatformTime::Seconds() - StartLeftAt;
		if (StartContent.IsValid()) StartContent->SetRenderOpacity(1.f - FMath::SmoothStep(0.f, 1.f, static_cast<float>(Since / StartTitleSeconds)));
		if (bStartScreen && Since >= StartTitleSeconds)
		{
			bStartScreen = false;
			StartContent.Reset();
			if (!bInGame && !SetupForm.IsValid() && !Finale.IsValid()) ShowMenu(true);
		}
		if (Since >= StartClearSeconds) StartLeftAt = 0;
	}
	// The window is named for the game, not for the project file and the kind of build.
	if (!bWindowNamed && !GIsEditor && GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
	{
		bWindowNamed = true;
		GEngine->GameViewport->GetWindow()->SetTitle(FText::FromString(TEXT("Ports of Plague")));
	}
	TestScriptTick();
	if (LobbyRoom.IsValid()) { LobbyRoom->Tick(FPlatformTime::Seconds()); TestLobbyPlay(); }
	// The tutorial's room: the first device to join is the player, and the game begins.
	if (bTutorialLobby && LobbyRoom.IsValid() && LobbyRoom->bReady)
	{
		const FPortsSeat* Seat = LobbyRoom->Seats.FindByPredicate([](const FPortsSeat& S) { return !S.bot; });
		if (Seat) { const FPortsSeat Joined = *Seat; BeginTutorial(&Joined); }
	}
	if (Room.IsValid())
	{
		Room->Tick(FPlatformTime::Seconds());
		// For checking the game: -PortsNetDropAt=20 cuts the connection that many seconds into the game.
		if (TestDropAt < 0) { float At = 0; TestDropAt = FParse::Value(FCommandLine::Get(), TEXT("PortsNetDropAt="), At) ? Now() + At : 0; }
		if (TestDropAt > 0 && bInGame && Now() >= TestDropAt) { TestDropAt = 0; Room->TestDrop(); }
		// The devices' clocks follow this one: they are told whenever it stops or starts.
		const bool bHold = bInGame && (Root->HasStoryDialog() || Map->IsAnimating() || Steps.Num() > 0);
		if (bHold != bLastHold) { bLastHold = bHold; PushToDevices(); }
		if (PushAt > 0 && FPlatformTime::Seconds() >= PushAt)
		{
			PushAt = 0;
			if (bInGame || State.phase == TEXT("ended")) Room->PushState(State.ToValue(), DeviceView());
		}
	}
	TickAudio(DeltaSeconds);
	HandleKeys();
	if (Finale.IsValid()) { FinaleTick(DeltaSeconds); return; }
	if (bPumpAgain) { bPumpAgain = false; Pump(); }
	if (!bInGame) return;

	// While a house's ship or cart is on its way the camera travels with it.
	if (Map->IsAnimating() && !bAutoPlay) if (APortsCameraPawn* Pawn = Camera()) Pawn->Follow(Map->GetShipmentPixel(), 2.6);

	// A hold ends when its time is up, or at once on a click, Enter or Space.
	if (HoldUntil > 0)
	{
		const APlayerController* PC = Map->GetWorld()->GetFirstPlayerController();
		const bool bSkip = PC && (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar));
		if (bSkip || Now() >= HoldUntil) { HoldUntil = 0; Pump(); }
	}

	if (bBotWaiting && Now() >= BotMoveAt && !Root->HasDialog() && !Map->IsAnimating()) BotStep();

	// When checking the game by itself, cards and choices close after a moment.
	if (bAutoPlay && Root->HasDialog())
	{
		if (AutoCloseAt <= 0) AutoCloseAt = Now() + 0.12;
		else if (Now() >= AutoCloseAt) { AutoCloseAt = 0; Root->CloseTop(TEXT("ok")); }
	}

	// Checking one action picker: the opening cards close by themselves, then the picker opens.
	if (!TestPrompt.IsEmpty())
	{
		const FPortsPlayer* P = Ports::CurrentPlayer(State);
		if (Root->HasDialog())
		{
			if (AutoCloseAt <= 0) AutoCloseAt = Now() + 0.15;
			else if (Now() >= AutoCloseAt) { AutoCloseAt = 0; Root->CloseTop(Root->TopIsSide() ? FString() : FString(TEXT("no"))); }
		}
		else if (P && !P->bot && !Busy())
		{
			const FString Id = TestPrompt;
			TestPrompt.Reset();
			if (Id == TEXT("endenter"))
			{
				// Checking that Enter on the "End your turn?" card ends the turn.
				const int32 Before = State.turn;
				TryEndTurn();
				const bool bOpened = Root->HasDialog();
				Root->ConfirmTop();
				UE_LOG(LogTemp, Display, TEXT("PortsCheck: end-turn card opened %d; turn %d -> %d; card still open %d."), bOpened, Before, State.turn, Root->HasDialog());
			}
			else if (Id == TEXT("decision")) { if (P->pending.Num()) OpenDecisionPrompt(); }
			else StartAction(Id);
		}
	}

	// The turn timer runs only while the house can actually choose.
	if (bClockOn)
	{
		const bool bHold = Root->HasStoryDialog() || Map->IsAnimating() || Steps.Num() > 0;
		if (!bHold) ClockLeft -= DeltaSeconds;
		const int32 Shown = FMath::CeilToInt32(ClockLeft) * 2 + (bHold ? 1 : 0);
		if (Shown != ClockShown) { ClockShown = Shown; RefreshTopBar(); }
		if (ClockLeft <= 0) ExpireTurn();
	}
}
