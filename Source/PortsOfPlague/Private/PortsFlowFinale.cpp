// The finale: seven scenes played on the board before the results page, after finale.js in
// the web version. Its sentences, numbers and timings are the web version's; what is new is
// that each scene happens on the 3D map, under a camera that moves for it.
#include "PortsGameFlow.h"

#include "PortsCameraPawn.h"
#include "PortsData.h"
#include "PortsFinale.h"
#include "PortsGameMode.h"
#include "PortsMapActor.h"
#include "PortsMapSpace.h"
#include "PortsRules.h"
#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

using V = FPortsValue;

namespace
{
	FString Plural(int32 N, const TCHAR* One, const TCHAR* Many = nullptr)
	{
		return FString::Printf(TEXT("%d %s"), N, N == 1 ? One : Many ? Many : *(FString(One) + TEXT("s")));
	}

	int32 Cfg(const TCHAR* Path) { return FPortsData::Get().Int(Path); }

	FString CityName(const FString& Id)
	{
		const FPortsCity* City = FPortsData::Get().FindCity(Id);
		return City ? City->Name : Id;
	}

	const FPortsCity* HomeOf(const FPortsPlayer& P) { return FPortsData::Get().FindCity(P.home); }

	// Where a house's figures, candle or tower stand: just south of its home city.
	FVector2D Beside(const FPortsPlayer& P, double East, double South)
	{
		return PortsMapSpace::CityPixel(*HomeOf(P)) + FVector2D(East, South);
	}
}

void UPortsGameFlow::ShowFinale()
{
	bInGame = false;
	bClockOn = false;
	if (!State.finalScores.IsSet() || !State.winner.IsSet()) { ShowEnd(); return; }
	if (!bAutoPlay) ClearSave();
	Root->CloseAllSilently();
	if (Map) Map->ApplyState(State);
	const FPortsData& Data = FPortsData::Get();

	Finale = MakeShared<FPortsFinaleModel>();
	FPortsFinaleModel& M = *Finale;
	M.Player = Map->GetWorld()->GetFirstPlayerController();

	// The facts about this game, worded as summary() in finale.js words them.
	const int32 TotalStart = State.players.Num() * Cfg(TEXT("start.family"));
	int32 Lost = 0, Rounds = 0;
	TArray<FString> Stood, EarlyNames;
	FinaleShips.Reset();
	FinaleEarly.Reset();
	for (const FPortsPlayer& P : State.players)
	{
		Lost += P.lostFamily;
		if (P.stats.protectedCount > 0) Stood.Add(P.name);
		FPortsFinaleHouse House;
		House.Name = P.name;
		House.Crest = P.crest;
		House.Home = CityName(P.home);
		House.Color = PortsUi::Color(*P.color);
		House.Spot = Map->CityLocation(P.home);
		House.Alive = Ports::FamilyTotal(P);
		House.Lost = P.lostFamily;
		House.bStood = P.stats.protectedCount > 0;
		M.Houses.Add(House);
	}
	for (const V& E : State.log)
	{
		const FString Type = E.Get(TEXT("type")).AsString();
		if (Type == TEXT("round")) Rounds++;
		else if (Type == TEXT("arrival") && E.Get(TEXT("early")).Truthy()) { EarlyNames.Add(CityName(E.Get(TEXT("city")).AsString())); FinaleEarly.Add(E.Get(TEXT("city")).AsString()); }
		else if (Type == TEXT("ship") && E.Get(TEXT("infected")).Truthy())
		{
			const int32 Who = E.Get(TEXT("player")).AsInt();
			FinaleShips.Add({ E.Get(TEXT("route")).AsString(), E.Get(TEXT("from")).AsString(), E.Get(TEXT("to")).AsString(), State.players.IsValidIndex(Who) ? PortsUi::Color(*State.players[Who].color) : PortsUi::Color(TEXT("#888888")) });
		}
	}
	// Checking the finale's "Plague arrived early" stamps when the game played had none.
	if (bTestEarly && FinaleEarly.Num() == 0)
	{
		for (const TCHAR* Id : { TEXT("marseille"), TEXT("london"), TEXT("bruges"), TEXT("lubeck") }) if (Data.FindCity(Id)) { FinaleEarly.Add(Id); EarlyNames.Add(CityName(Id)); }
	}
	M.Infected = FinaleShips.Num();
	M.Pct = FMath::RoundToInt32(100.0 * Lost / FMath::Max(1, TotalStart));
	TArray<FString> WinNames;
	for (const int32 Id : *State.winner) { WinNames.Add(State.players[Id].name); M.Winners.Add(Id); }
	M.WinnerNames = FString::Join(WinNames, TEXT(" & "));
	M.WinLine = FString::Printf(TEXT("%s %s with the greatest Legacy."), *FString::Join(WinNames, TEXT(" and ")), WinNames.Num() > 1 ? TEXT("share the victory") : TEXT("wins"));
	M.LostLine = FString::Printf(TEXT("Your houses lost <b>%d</b> of %d family members (%d%%). Historians estimate that between a third and 60 percent of Europeans died."), Lost, TotalStart, M.Pct);
	M.ShipLine = FString::Printf(TEXT("Your ships carried infected cargo <b>%d</b> time%s, bringing the plague early to %s. In real history, trade routes carried the plague across Europe."),
		M.Infected, M.Infected == 1 ? TEXT("") : TEXT("s"), EarlyNames.Num() ? *FString::Join(EarlyNames, TEXT(", ")) : TEXT("no city"));
	M.StandLine = FString::Printf(TEXT("%s In 1349, the people who tried were overruled; the accusations were false and the violence unjust."),
		Stood.Num() ? *FString::Printf(TEXT("%s took a stand to protect a persecuted community."), *FString::Join(Stood, TEXT(", "))) : TEXT("No house took a stand to protect the persecuted community."));
	M.MetaLine = FString::Printf(TEXT("%s · %s played · %s discovered"), *Plural(State.players.Num(), TEXT("house")), *Plural(Rounds, TEXT("round")), *Plural(State.journal.Num(), TEXT("fact"))).ToUpper();

	// The Toll: the lost fall one by one, taking turns between the houses.
	{
		int32 Doomed = 0, Most = 0;
		for (const FPortsFinaleHouse& H : M.Houses) { Doomed += H.Lost; Most = FMath::Max(Most, H.Lost); }
		const double Step = Doomed ? FMath::Clamp(3.4 / Doomed, 0.09, 0.26) : 0.0;
		M.TollStart = 1.9;
		int32 Order = 0;
		for (int32 k = 0; k < Most; k++) for (FPortsFinaleHouse& H : M.Houses) if (k < H.Lost) H.FallAt.Add(M.TollStart + (Order++) * Step);
		M.TollFall = Doomed * Step;
		M.TollTextAt = M.TollStart + M.TollFall + 0.5;
	}
	// Plague Ships: at most a dozen voyages, spread over the whole game; then the cities reached early.
	if (FinaleShips.Num() > 12)
	{
		TArray<FFinaleShip> Few;
		for (int32 i = 0; i < 12; i++) Few.Add(FinaleShips[FMath::RoundToInt32(i * (FinaleShips.Num() - 1) / 11.0)]);
		FinaleShips = Few;
	}
	FinaleGap = FinaleShips.Num() ? FMath::Min(0.65, 3.8 / FinaleShips.Num()) : 0.0;
	M.ReplayEnd = FinaleShips.Num() ? 0.9 + (FinaleShips.Num() - 1) * FinaleGap + 2.4 : 0.9;
	M.EarlyMore = FMath::Max(0, EarlyNames.Num() - 10);
	if (EarlyNames.Num() > 10) { EarlyNames.SetNum(10); FinaleEarly.SetNum(10); }
	M.Early = EarlyNames;
	M.CaptionAt = M.ReplayEnd + (M.Early.Num() ? 0.6 + M.Early.Num() * 0.45 : 0.2);

	// Honours: the most of something, held by one or two houses but not by everyone; up to six (awards in finale.js).
	{
		struct FAward { const TCHAR* Title; TFunction<int32(const FPortsPlayer&)> Value; TFunction<FString(int32)> What; };
		const FAward Awards[] = {
			{ TEXT("Master of the Seas"), [](const FPortsPlayer& P) { return P.stats.shipments; }, [](int32 N) { return Plural(N, TEXT("shipment")); } },
			{ TEXT("The Survivors"), [](const FPortsPlayer& P) { return Ports::FamilyTotal(P); }, [](int32 N) { return Plural(N, TEXT("family member")) + TEXT(" alive"); } },
			{ TEXT("Fullest Coffers"), [](const FPortsPlayer& P) { return P.florins; }, [](int32 N) { return FString::Printf(TEXT("%dƒ in the strongbox"), N); } },
			{ TEXT("Pillar of Charity"), [](const FPortsPlayer& P) { return P.stats.charity; }, [](int32 N) { return Plural(N, TEXT("act")) + TEXT(" of charity"); } },
			{ TEXT("Fortune’s Favourite"), [](const FPortsPlayer& P) { return P.stats.fortune; }, [](int32 N) { return Plural(N, TEXT("Fortune card")); } },
			{ TEXT("Unlucky Cargo"), [](const FPortsPlayer& P) { return P.stats.infected; }, [](int32 N) { return Plural(N, TEXT("infected shipment")); } },
			{ TEXT("Web of Trade"), [](const FPortsPlayer& P) { return P.posts.Num(); }, [](int32 N) { return Plural(N, TEXT("trading post")); } },
			{ TEXT("Always on the Road"), [](const FPortsPlayer& P) { return P.stats.fled; }, [](int32 N) { return TEXT("fled ") + Plural(N, TEXT("time")); } },
			{ TEXT("Best Reputation"), [](const FPortsPlayer& P) { return P.reputation; }, [](int32 N) { return FString::Printf(TEXT("%d reputation"), N); } },
			{ TEXT("The Matchmaker"), [](const FPortsPlayer& P) { return P.stats.married; }, [](int32 N) { return Plural(N, TEXT("marriage")); } },
			{ TEXT("Lord of the Land"), [](const FPortsPlayer& P) { return P.land.Num(); }, [](int32 N) { return Plural(N, TEXT("estate")) + TEXT(" of land"); } },
		};
		for (int32 a = 0; a < UE_ARRAY_COUNT(Awards) && M.Honours.Num() < 6; a++)
		{
			int32 Best = MIN_int32;
			for (const FPortsPlayer& P : State.players) Best = FMath::Max(Best, Awards[a].Value(P));
			TArray<int32> Holders;
			for (int32 i = 0; i < State.players.Num(); i++) if (Awards[a].Value(State.players[i]) == Best) Holders.Add(i);
			if (Best <= 0 || Holders.Num() > 2 || Holders.Num() == State.players.Num()) continue;
			M.Honours.Add({ a, Awards[a].Title, Awards[a].What(Best), Holders });
		}
	}
	// The Reckoning: from last place to first, then a pause before the winner.
	{
		for (const FPortsRank& R : *State.finalScores)
		{
			FPortsFinaleRow Row;
			Row.Place = R.place; Row.House = R.id; Row.Total = R.total;
			Row.Parts[0] = R.wealth; Row.Parts[1] = R.family; Row.Parts[2] = R.reputation; Row.Parts[3] = R.balance;
			M.Rows.Add(Row);
			M.MaxTotal = FMath::Max(M.MaxTotal, R.total);
			if (R.place == 1) M.TopTotal = R.total;
		}
		double T = 0.9;
		for (int32 i = M.Rows.Num() - 1; i >= 0; i--) if (M.Rows[i].Place != 1) { M.Rows[i].At = T; T += 1.9; }
		M.SuspenseAt = T;
		T += 2.6;
		M.FlashAt = T;
		for (FPortsFinaleRow& Row : M.Rows) if (Row.Place == 1) Row.At = T;
	}

	M.SceneIds = { TEXT("title"), TEXT("toll"), TEXT("ships"), TEXT("vigil") };
	M.SceneLabels = { TEXT("Anno Domini"), TEXT("The Toll"), TEXT("Plague Ships"), TEXT("Conscience") };
	if (M.Honours.Num()) { M.SceneIds.Add(TEXT("honours")); M.SceneLabels.Add(TEXT("Honours")); }
	M.SceneIds.Append({ TEXT("reckoning"), TEXT("crown") });
	M.SceneLabels.Append({ TEXT("The Reckoning"), TEXT("Victory") });

	TWeakObjectPtr<UPortsGameFlow> Weak(this);
	Root->SetScreen(SNew(SPortsFinale).Model(Finale).OnMove([Weak](int32 Code)
	{
		UPortsGameFlow* Flow = Weak.Get();
		if (!Flow || !Flow->Finale.IsValid()) return;
		if (Code == 99) Flow->FinaleFinish();
		else if (Code == 0) Flow->Finale->bPaused = !Flow->Finale->bPaused;
		else if (Code >= 1000) Flow->FinaleShow(Code - 1000);
		else Flow->FinaleShow(Flow->Finale->Scene + Code);
	}));
	FinaleShow(TestFinale >= 0 && TestFinale < 100 ? FMath::Min(TestFinale, M.SceneIds.Num() - 1) : 0);
}

// Starts a scene: clears the board of the last one's props and sets how long this one stays.
void UPortsGameFlow::FinaleShow(int32 Index)
{
	if (!Finale.IsValid()) return;
	FPortsFinaleModel& M = *Finale;
	if (Index >= M.SceneIds.Num()) { FinaleFinish(); return; }
	M.Scene = FMath::Max(0, Index);
	M.T = 0;
	FinaleLaunched = 0;
	FinaleStamped = 0;
	Map->ClearFinale();
	Map->ShowStainsAt(TArray<FString>());
	const FString& Id = M.SceneIds[M.Scene];
	// The board is cleared of its lettering and markers; only the sailing scene keeps the cities' names.
	Map->SetQuiet(Id == TEXT("ships") ? 1 : 2);
	const int32 N = M.Houses.Num();
	M.Hold = Id == TEXT("title") ? 7.6 : Id == TEXT("toll") ? M.TollTextAt + 7.5 : Id == TEXT("ships") ? M.CaptionAt + 8.0 : Id == TEXT("vigil") ? 1.2 + N * 0.45 + 8.5
		: Id == TEXT("honours") ? 0.8 + M.Honours.Num() * 0.7 + 6.5 : Id == TEXT("reckoning") ? M.FlashAt + 6.5 : 11.0;
	const float Dark = Id == TEXT("title") ? 0.8f : Id == TEXT("toll") ? 0.62f : Id == TEXT("ships") ? 0.5f : Id == TEXT("vigil") ? 1.f : Id == TEXT("honours") ? 0.75f : Id == TEXT("reckoning") ? 0.42f : 0.42f;
	if (APortsGameMode* Game = Map->GetWorld()->GetAuthGameMode<APortsGameMode>()) Game->SetNight(Dark);

	// The scene's sounds, at the moments finale.js plays them. Anything still waiting from the last scene is dropped.
	LaterSounds.Reset();
	const auto At = [this](const TCHAR* Name, double Seconds) { if (Seconds <= 0) Sound(Name); else Sound(Name, Seconds); };
	if (Id == TEXT("title"))
	{
		At(TEXT("knell"), 0);
		for (int32 i = 0; i < 4; i++) At(TEXT("stamp"), 1.5 + i * 0.28);
	}
	else if (Id == TEXT("toll")) { if (M.TollFall > 0) At(TEXT("knell"), M.TollStart); }
	else if (Id == TEXT("ships"))
	{
		if (FinaleShips.Num()) At(TEXT("sail"), 0.9);
		if (FinaleEarly.Num()) At(TEXT("plague"), M.ReplayEnd);
		for (int32 i = 0; i < FinaleEarly.Num(); i++) At(TEXT("stamp"), M.ReplayEnd + 0.3 + i * 0.45);
	}
	else if (Id == TEXT("honours")) { for (int32 i = 0; i < M.Honours.Num(); i++) At(TEXT("page"), 0.8 + i * 0.7); }
	else if (Id == TEXT("reckoning"))
	{
		for (const FPortsFinaleRow& Row : M.Rows) { At(TEXT("cart"), Row.At); At(TEXT("coin"), Row.At + 0.35); }
		At(TEXT("drumroll"), M.SuspenseAt);
		At(TEXT("fanfare"), M.FlashAt + 1.5);
	}
	else if (Id == TEXT("crown")) At(TEXT("victory"), 0);
}

void UPortsGameFlow::FinaleFinish()
{
	if (!Finale.IsValid()) return;
	Finale.Reset();
	LaterSounds.Reset();
	Map->ClearFinale();
	Map->SetQuiet(0);
	if (APortsGameMode* Game = Map->GetWorld()->GetAuthGameMode<APortsGameMode>()) Game->SetNight(0.f);
	if (APortsCameraPawn* Pawn = Camera()) Pawn->EndShot();
	ShowEnd();
}

void UPortsGameFlow::FinaleTick(float DeltaSeconds)
{
	FPortsFinaleModel& M = *Finale;
	APlayerController* PC = Map->GetWorld()->GetFirstPlayerController();
	APortsCameraPawn* Pawn = Camera();
	if (!PC || !Pawn) return;

	// Right, Page Down, Space or Enter for the next scene; Left or Page Up for the one before; P to pause; Esc for the results.
	if (PC->WasInputKeyJustPressed(EKeys::Escape)) { FinaleFinish(); return; }
	if (PC->WasInputKeyJustPressed(EKeys::Right) || PC->WasInputKeyJustPressed(EKeys::PageDown) || PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Enter)) { FinaleShow(M.Scene + 1); return; }
	if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::PageUp)) { FinaleShow(M.Scene - 1); return; }
	if (PC->WasInputKeyJustPressed(EKeys::P)) M.bPaused = !M.bPaused;

	if (!M.bPaused) M.T += DeltaSeconds;
	if (TestFinaleAt >= 0) M.T = FMath::Min(M.T, TestFinaleAt);
	if (M.T >= M.Hold && (TestFinale < 0 || TestFinale >= 100)) { FinaleShow(M.Scene + 1); return; }
	const double T = M.T;
	const FString& Id = M.SceneIds[M.Scene];
	const auto Ease = [](double X) { const double U = FMath::Clamp(X, 0.0, 1.0); return 1.0 - FMath::Pow(1.0 - U, 3.0); };

	// The middle of the houses' homes, and how close the camera can come with all of them in view.
	FBox2D Homes(ForceInit);
	for (const FPortsPlayer& P : State.players) Homes += PortsMapSpace::CityPixel(*HomeOf(P));
	const FVector2D Need = Homes.GetSize() + FVector2D(330.0, 250.0);
	const double Fit = FMath::Clamp(FMath::Min(PortsProjection::MapWidth() * 1.25 / Need.X, PortsProjection::MapHeight() * 0.95 / Need.Y), 1.0, 2.4);
	const FVector2D Whole(PortsProjection::MapWidth() * 0.5, PortsProjection::MapHeight() * 0.5);

	FPortsFinaleProps Props;
	Props.Time = Map->GetWorld()->GetTimeSeconds();
	const FRotationMatrix Looking(PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator);
	Props.Right = Looking.GetScaledAxis(EAxis::Y);
	Props.Up = Looking.GetScaledAxis(EAxis::Z);

	if (Id == TEXT("title"))
	{
		// Night over the whole board, sparks rising, the camera low and slowly turning.
		Props.Embers = 150;
		Pawn->SetShot(Whole + FVector2D(0, 40), 1.22 + 0.02 * T, 38.0, -9.0 + 2.2 * T);
	}
	else if (Id == TEXT("toll"))
	{
		// Each house's family stands in a row by its home; the lost fall one by one.
		Props.Embers = 40;
		for (int32 h = 0; h < M.Houses.Num(); h++)
		{
			const FPortsFinaleHouse& House = M.Houses[h];
			const int32 Count = House.Alive + House.Lost;
			for (int32 k = 0; k < Count; k++)
			{
				FPortsFinaleProps::FFigure F;
				F.At = Beside(State.players[h], (k - (Count - 1) * 0.5) * 8.2, 20.0);
				F.Color = House.Color;
				F.Pop = static_cast<float>(Ease((T - (0.5 + k * 0.04 + h * 0.12)) / 0.45));
				const int32 Doomed = k - House.Alive;
				F.Fall = Doomed >= 0 && House.FallAt.IsValidIndex(Doomed) ? static_cast<float>(FMath::Clamp((T - House.FallAt[Doomed]) / 0.6, 0.0, 1.0)) : 0.f;
				Props.Figures.Add(F);
			}
		}
		Pawn->SetShot(Homes.GetCenter() + FVector2D(-70.0 / Fit, 46.0 / Fit), Fit * (0.74 + 0.008 * T), 50.0, -4.0 + 0.5 * T);
	}
	else if (Id == TEXT("ships"))
	{
		// The whole board from above; the infected cargoes sail again, and the cities reached early are stained.
		Pawn->SetShot(Whole + FVector2D(0, 30), 1.06, 60.0, 0.0);
		while (FinaleLaunched < FinaleShips.Num() && T >= 0.9 + FinaleLaunched * FinaleGap)
		{
			const FFinaleShip Ship = FinaleShips[FinaleLaunched++];
			TWeakObjectPtr<UPortsGameFlow> Weak(this);
			Map->ReplayVoyage(Ship.Route, Ship.From, Ship.Color, [Weak, To = Ship.To]() { if (Weak.IsValid() && Weak->Finale.IsValid()) Weak->Map->FloatText(To, TEXT("Infected!"), false); });
		}
		int32 Due = 0;
		while (Due < FinaleEarly.Num() && T >= M.ReplayEnd + 0.3 + Due * 0.45) Due++;
		if (Due != FinaleStamped)
		{
			for (int32 i = FinaleStamped; i < Due; i++) Map->FloatText(FinaleEarly[i], TEXT("Early!"), false);
			FinaleStamped = Due;
			TArray<FString> Stained = FinaleEarly;
			Stained.SetNum(Due);
			Map->ShowStainsAt(Stained);
		}
	}
	else if (Id == TEXT("vigil"))
	{
		// Deep night. A candle stands by each home, lit for a house that took a stand, and lights the land round it.
		Props.Embers = 30;
		for (int32 h = 0; h < M.Houses.Num(); h++) Props.Candles.Add({ Beside(State.players[h], 0, 19.0), M.Houses[h].bStood, static_cast<float>(Ease((T - (0.6 + h * 0.45)) / 0.9)) });
		Pawn->SetShot(Homes.GetCenter() + FVector2D(0, 60.0 / Fit), Fit * (1.04 + 0.01 * T), 37.0, 5.0 - 0.6 * T);
	}
	else if (Id == TEXT("honours"))
	{
		Props.Embers = 90;
		Pawn->SetShot(Whole, 1.5, 30.0, 14.0 + 1.6 * T);
	}
	else if (Id == TEXT("reckoning"))
	{
		// Each house's Legacy rises as a tower by its home as its row comes in, storey by storey.
		for (const FPortsFinaleRow& Row : M.Rows)
		{
			FPortsFinaleProps::FTower Tower;
			Tower.At = Beside(State.players[Row.House], 0, 19.0);
			Tower.bWinner = Row.Place == 1 && T >= Row.At + 1.6;
			for (int32 k = 0; k < 4; k++) Tower.Parts[k] = T < Row.At ? 0.f : static_cast<float>(Row.Parts[k] * 1.05 * Ease((T - Row.At - 0.35 - k * 0.3) / 0.7));
			Props.Towers.Add(Tower);
		}
		// The homes sit in the upper half of the picture, clear of the rows of scores below.
		Pawn->SetShot(Homes.GetCenter() + FVector2D(0, 215.0 / Fit), Fit * 0.86, 40.0, -3.0 + 0.25 * T);
	}
	else if (Id == TEXT("crown"))
	{
		// The camera sweeps down to the winner's city and circles it while the crown descends.
		const FPortsPlayer& Winner = State.players[M.Winners[0]];
		Props.Embers = 60;
		Props.bCrown = true;
		Props.CrownAt = PortsMapSpace::CityPixel(*HomeOf(Winner));
		Props.CrownDrop = static_cast<float>((T - 0.4) / 2.6);
		for (const int32 W : M.Winners) Props.Confetti.Add(PortsUi::Color(*State.players[W].color));
		Props.Confetti.Append({ PortsUi::Color(TEXT("#b0261a")), PortsUi::Color(TEXT("#fff3c4")), PortsUi::Color(TEXT("#1d4a86")) });
		Pawn->SetShot(Props.CrownAt + FVector2D(0, 16), 4.6 + 0.06 * T, 21.0, -24.0 + 7.5 * T);
	}
	Map->DrawFinale(Props);
}
