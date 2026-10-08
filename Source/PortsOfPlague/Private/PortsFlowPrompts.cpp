// The choices a house makes on its turn: the house panel, the action buttons,
// the action pickers and the decision cards, as in src/ui/prompts.js.
#include "PortsGameFlow.h"

#include "PortsMapActor.h"
#include "PortsSettings.h"
#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

using V = FPortsValue;
using PortsUi::Esc;
using PortsUi::Rich;
using PortsUi::EButton;

namespace
{
	const FPortsData& Data() { return FPortsData::Get(); }
	int32 Cfg(const TCHAR* Path) { return FPortsData::Get().Int(Path); }

	FString CityName(const FString& Id)
	{
		if (Id == Ports::ESTATE) return TEXT("Country Estate");
		const FPortsCity* City = Data().FindCity(Id);
		return City ? City->Name : Id;
	}

	FPortsAction CityAction(const FString& Type, const FString& City) { FPortsAction A; A.type = Type; A.city = City; return A; }
	FPortsAction ShipAction(const FString& From, const FString& Route, bool bOffshore = false) { FPortsAction A; A.type = TEXT("ship"); A.from = From; A.route = Route; A.offshore = bOffshore; return A; }

	// A half-year by name. Something agreed in the final round lasts "until the end of
	// next round", which is past the last half-year: the end of the game.
	FString UntilLabel(int32 Half)
	{
		const V& Info = Half <= Cfg(TEXT("rounds")) ? Ports::HalfInfo(Half) : V();
		return Info.IsObject() ? Info.Get(TEXT("label")).AsString() : FString(TEXT("the end of the game"));
	}

	FString StatusOf(const FPortsState& State, const FString& City)
	{
		const FString& S = State.City(City)->state;
		return S == TEXT("aftermath") ? TEXT("Aftermath") : S == TEXT("stricken") ? TEXT("Stricken") : Ports::IsThreatened(State, City) ? TEXT("Threatened") : TEXT("Safe");
	}

	const V& ActionInfo(const FString& Id)
	{
		static const V None;
		for (const V& A : Data().Actions().GetItems()) if (A.Get(TEXT("id")).AsString() == Id) return A;
		return None;
	}

	// null if any of the actions is possible, otherwise the most useful reason why not.
	FString FirstReason(const FPortsState& State, const TArray<FPortsAction>& Actions)
	{
		TArray<FString> Reasons;
		for (const FPortsAction& A : Actions) Reasons.Add(Ports::CheckAction(State, A));
		if (Reasons.Contains(FString())) return FString();
		for (const FString& R : Reasons) if (!R.StartsWith(TEXT("Choose"))) return R;
		return Reasons.Num() ? Reasons[0] : FString(TEXT("Not possible right now."));
	}
}

void UPortsGameFlow::Open(TFunction<TSharedRef<SWidget>(TFunction<void(const FString&)>)> Build, const FPortsDialogOptions& Options, TFunction<void(const FString&)> OnClose)
{
	TWeakObjectPtr<UPortsGameFlow> Weak(this);
	Root->OpenDialog(Build, Options, [Weak, OnClose](const FString& Value)
	{
		if (!Weak.IsValid()) return;
		if (OnClose) OnClose(Value);
		Weak->Pump();
	});
}

// ---------- House panel and action buttons ----------

void UPortsGameFlow::AddHousePanel(FPortsDoc& Doc, const FPortsPlayer& P) const
{
	const FPortsScore Sc = Ports::ScorePlayer(P);
	const int32 ApMax = FMath::Max(P.ap, Ports::ActionPointsFor(State, P));
	const int32 Estate = Ports::FamilyAt(P, Ports::ESTATE);
	FPortsDoc Panel;
	TArray<FString> Posts;
	for (const FString& C : P.posts) Posts.Add(Esc(CityName(C)));
	Panel.Small(FString::Printf(TEXT("Home: %s · Posts: %s"), *Esc(CityName(P.home)), *FString::Join(Posts, TEXT(", "))));
	// .stat: three pale boxes with an icon, a number and what it counts.
	const int32 Florins = P.florins, Reputation = P.reputation, Family = Ports::FamilyTotal(P);
	Panel.AddBuilt([Florins, Reputation, Family, Estate](float W)
	{
		const float Each = W > 0.f ? (W - 12.f) / 3.f : 0.f;
		const auto Stat = [Each](const TCHAR* Icon, int32 Value, const FString& Label) -> TSharedRef<SWidget>
		{
			return PortsUi::Box(PortsUi::PlainLook(PortsUi::Color(TEXT("#fffdf6")), 10, PortsUi::Color(TEXT("#d8bc7c")), 1), SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[ PortsUi::Picture(Icon, FVector2D(20, 20)) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Rich(FString::FromInt(Value), TEXT("Ports.Stat"), ETextJustify::Left, false) ]
				]
				+ SVerticalBox::Slot().AutoHeight()[ Rich(Label, TEXT("Ports.Small"), ETextJustify::Center, true, Each > 0.f ? FMath::Max(40.f, Each - 10.f) : 0.f) ], FMargin(4, 4));
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 3, 0)[ Stat(TEXT("icon_coin"), Florins, TEXT("Florins")) ]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(3, 0)[ Stat(TEXT("icon_laurel"), Reputation, TEXT("Reputation")) ]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(3, 0, 0, 0)[ Stat(TEXT("icon_family"), Family, Estate ? FString::Printf(TEXT("Family (%d at estate)"), Estate) : FString(TEXT("Family"))) ];
	}, FMargin(0, 5));
	// Action points as candles: lit ones are still to spend.
	const bool bFavor = State.guildFavor.IsSet() && *State.guildFavor == P.id;
	const TSharedRef<SHorizontalBox> Ap = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[ Rich(TEXT("<b>Action points:</>"), TEXT("Ports.Body"), ETextJustify::Left, false) ];
	for (int32 i = 0; i < ApMax; i++) Ap->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)[ PortsUi::Picture(i < P.ap ? TEXT("icon_candle_lit") : TEXT("icon_candle_out"), FVector2D(15.4, 22)) ];
	Ap->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(7, 0, 0, 0)[ Rich(FString::Printf(TEXT("<b>%d left%s</>"), P.ap, bFavor ? TEXT(" · Guild’s Favor") : TEXT("")), TEXT("Ports.Body"), ETextJustify::Left, false) ];
	Panel.Add(Ap, FMargin(0, 5, 0, 2));
	// The Legacy bar: wealth, family, reputation and the balance bonus, in the website's colours.
	const int32 Total = FMath::Max(1, Sc.total);
	const TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	const auto Part = [&Bar, Total](int32 Value, const TCHAR* Hex)
	{
		if (Value > 0) Bar->AddSlot().FillWidth(static_cast<float>(Value) / Total)[ PortsUi::Box(PortsUi::PlainLook(PortsUi::Color(Hex), 0), SNew(SBox).HeightOverride(12)) ];
	};
	Part(Sc.wealth, TEXT("#d9a82b"));
	Part(Sc.family, TEXT("#8a3b2a"));
	Part(Sc.reputation, TEXT("#3f6b2a"));
	Part(Sc.balance, TEXT("#1d4a86"));
	Panel.Add(PortsUi::Box(PortsUi::PlainLook(PortsUi::Color(TEXT("#e6d6ae")), 6), Bar, FMargin(0)), FMargin(0, 4));
	Panel.Small(FString::Printf(TEXT("Legacy: Wealth %d + Family %d + Reputation %d + Balance %d = <sb>%d</>"), Sc.wealth, Sc.family, Sc.reputation, Sc.balance, Sc.total));

	// Fortune notes.
	TArray<FString> Notes;
	if (P.free.Get(TEXT("post"), false)) Notes.Add(TEXT("Next trading post is free"));
	if (P.free.Get(TEXT("physician"), false)) Notes.Add(TEXT("Next physician is free"));
	if (P.free.Get(TEXT("move"), false)) Notes.Add(TEXT("Next family move is free"));
	if (P.free.Get(TEXT("moveNoPenalty"), false)) Notes.Add(TEXT("Next family move is free, with no reputation loss"));
	if (P.free.Get(TEXT("prepare"), false)) Notes.Add(TEXT("Next Prepare Household is free"));
	if (P.nextShip.IsSet() && P.nextShip->profit) Notes.Add(FString::Printf(TEXT("Next shipment %s%dƒ"), P.nextShip->profit > 0 ? TEXT("+") : TEXT(""), P.nextShip->profit));
	if (P.nextShip.IsSet() && P.nextShip->safe) Notes.Add(TEXT("Next shipment cannot be infected"));
	if (P.personalCosts.Get(TEXT("openPost"), 0)) Notes.Add(FString::Printf(TEXT("Trading posts cost +%dƒ this round"), P.personalCosts.Get(TEXT("openPost"), 0)));
	if (Notes.Num()) Panel.Small(FString::Printf(TEXT("<sb>Fortune:</> %s"), *Esc(FString::Join(Notes, TEXT(" · ")))));

	// Land, debt, partnership and closed gates: the Merchant's Ledger at a glance.
	if (P.land.Num())
	{
		TArray<FString> Land;
		for (const FString& C : P.land) Land.Add(Esc(CityName(C)));
		Panel.Small(FString::Printf(TEXT("Land: %s (+%d Wealth, %dƒ wages each half-year)"), *FString::Join(Land, TEXT(", ")), P.land.Num() * Cfg(TEXT("scoring.pointsPerLand")), P.land.Num() * Cfg(TEXT("costs.landWage"))));
	}
	if (P.loan.IsSet()) Panel.Small(FString::Printf(TEXT("Debt: %dƒ due %s"), P.loan->owed, *Esc(UntilLabel(P.loan->due))));
	if (P.deal.IsSet()) Panel.Small(FString::Printf(TEXT("Partner: %s until %s"), *Esc(State.players[P.deal->partner].name), *Esc(UntilLabel(P.deal->until))));
	if (P.gates.IsSet()) Panel.Small(FString::Printf(TEXT("Gates closed: %s until %s"), *Esc(CityName(P.gates->city)), *Esc(UntilLabel(P.gates->until))));
	// The ribbon is in the house's own colour, with its crest.
	const TSharedRef<SWidget> Ribbon = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ PortsUi::Crest(P, 22) ]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Rich(Esc(P.name), TEXT("Ports.Ribbon"), ETextJustify::Left, false) ];
	Doc.Panel(Ribbon, PortsUi::Color(*P.color), Panel);
}

// A quick reason why an action is impossible right now (full checks run later).
FString UPortsGameFlow::QuickBlock(const FString& Id, const FPortsPlayer& P) const
{
	using namespace Ports;
	if (Busy()) return TEXT("Please wait…");
	if (P.pending.Num()) return TEXT("Answer the card first.");
	const bool bFree = Id == TEXT("loan") || Id == TEXT("deal") || (Id == TEXT("move") && (P.free.Get(TEXT("move"), false) || P.free.Get(TEXT("moveNoPenalty"), false)))
		|| (Id == TEXT("prepare") && P.free.Get(TEXT("prepare"), false)) || (Id == TEXT("post") && P.free.Get(TEXT("post"), false)) || (Id == TEXT("physician") && P.free.Get(TEXT("physician"), false));
	if (P.ap < 1 && !bFree) return TEXT("No action points left.");
	if (P.ap < ApCost(Id) && !bFree) return FString::Printf(TEXT("Takes %d action points."), ApCost(Id));
	if (Id == TEXT("ship") && LegalShipments(State, P).Num() == 0) return TEXT("All your posts have shipped this round.");
	const int32 PostCost = Cost(State, TEXT("openPost"), &P);
	if (Id == TEXT("post") && !P.free.Get(TEXT("post"), false) && P.florins < PostCost) return FString::Printf(TEXT("Needs %dƒ."), PostCost);
	if (Id == TEXT("post") && P.posts.Num() >= Cfg(TEXT("limits.maxPosts"))) return TEXT("Maximum number of posts.");
	if (Id == TEXT("post") && LegalPosts(State, P).Num() == 0) return TEXT("No connected city can take a new post right now.");
	if (Id == TEXT("charity")) { FPortsAction A; A.type = TEXT("charity"); A.kind = TEXT("church"); return CheckAction(State, A); }
	if (Id == TEXT("prepare") || Id == TEXT("physician"))
	{
		const TArray<FString> Homes = FamilyLocations(P);
		if (!Homes.ContainsByPredicate([&](const FString& L) { return L != ESTATE && CheckAction(State, CityAction(Id, L)).IsEmpty(); }))
		{
			const bool bAllEstate = !Homes.ContainsByPredicate([](const FString& L) { return L != ESTATE; });
			return bAllEstate ? FString(TEXT("All your family is at the estate.")) : FString::Printf(TEXT("Not possible right now (cost %dƒ, once per city)."), Id == TEXT("prepare") ? Cost(State, TEXT("prepareHousehold")) : Cost(State, TEXT("physician")));
		}
	}
	if ((Id == TEXT("marry") || Id == TEXT("land")) && !P.posts.ContainsByPredicate([&](const FString& C) { return IsAftermath(State, C); })) return TEXT("Opens when one of your cities reaches Aftermath.");
	if (Id == TEXT("marry") || Id == TEXT("land") || Id == TEXT("gates"))
	{
		TArray<FPortsAction> Options;
		for (const FString& C : P.posts) Options.Add(CityAction(Id, C));
		return FirstReason(State, Options);
	}
	if (Id == TEXT("loan")) { FPortsAction A; A.type = TEXT("loan"); return CheckAction(State, A); }
	if (Id == TEXT("deal"))
	{
		TArray<FPortsAction> Options;
		for (const FPortsPlayer& O : State.players) if (&O != &P) { FPortsAction A; A.type = TEXT("deal"); A.partner = O.id; Options.Add(A); }
		return FirstReason(State, Options);
	}
	return FString();
}

// The Actions panel: one button per action (with the reason when blocked) and End turn.
void UPortsGameFlow::AddActionsPanel(FPortsDoc& Doc, const FPortsPlayer& P)
{
	using namespace Ports;
	FPortsDoc Panel;
	const auto CostText = [&](const FString& Id) -> FString
	{
		const auto F = [&P](const TCHAR* Key) { return P.free.Get(Key, false); };
		if (Id == TEXT("ship")) return TEXT("1 AP");
		if (Id == TEXT("post")) return F(TEXT("post")) ? FString(TEXT("free")) : FString::Printf(TEXT("%d AP · %dƒ"), ApCost(TEXT("post")), Cost(State, TEXT("openPost"), &P));
		if (Id == TEXT("move")) return F(TEXT("move")) || F(TEXT("moveNoPenalty")) ? FString(TEXT("free")) : FString::Printf(TEXT("%d AP"), ApCost(TEXT("move")));
		if (Id == TEXT("prepare")) return F(TEXT("prepare")) ? FString(TEXT("free")) : FString::Printf(TEXT("1 AP · %dƒ"), Cost(State, TEXT("prepareHousehold")));
		if (Id == TEXT("physician")) return F(TEXT("physician")) ? FString(TEXT("free")) : FString::Printf(TEXT("1 AP · %dƒ"), Cost(State, TEXT("physician")));
		if (Id == TEXT("charity")) return FString::Printf(TEXT("1 AP · %dƒ"), CharityCost(State, P));
		if (Id == TEXT("marry")) return FString::Printf(TEXT("1 AP · %dƒ"), Cost(State, TEXT("marriage")));
		if (Id == TEXT("land")) return FString::Printf(TEXT("1 AP · %dƒ"), Cost(State, TEXT("buyLand")));
		if (Id == TEXT("gates")) return FString::Printf(TEXT("1 AP · −%d rep"), Cfg(TEXT("penalties.gatesReputation")));
		return TEXT("no AP");
	};
	// .action-btn in game.css: a round coloured icon, the action's name, what it does (or why not), its key and cost.
	FPortsBoxLook Look;
	Look.Top = PortsUi::Color(TEXT("#fffaf0")); Look.Bottom = PortsUi::Color(TEXT("#efdcae"));
	Look.Radius = 12;
	Look.Border = PortsUi::Color(TEXT("#3b2413")); Look.BorderWidth = 2;
	Look.Shadow = PortsUi::Color(TEXT("#8a6a36")); Look.ShadowDrop = 2;
	FPortsBoxLook Hover = Look;
	Hover.Top = PortsUi::Color(TEXT("#fffdf6")); Hover.Bottom = PortsUi::Color(TEXT("#f6e6bc"));
	const auto ActionButton = [&](const V& A)
	{
		const FString Id = A.Get(TEXT("id")).AsString();
		const FString Why = QuickBlock(Id, P);
		const FString Name = FString::Printf(TEXT("<caps>%s</>"), *Esc(A.Get(TEXT("name")).AsString()));
		const FString What = Esc(Why.IsEmpty() ? A.Get(TEXT("short")).AsString() : Why);
		const FString Key = FPortsSettings::Get().KeyLabel(FName(*Id)).ToUpper();
		const FString Price = CostText(Id);
		Panel.AddBuilt([this, Look, Hover, Id, Name, What, Key, Price, bOn = Why.IsEmpty()](float W)
		{
			// The key and cost keep a fixed room on the right, so the text beside them knows its width at once.
			const float Right = Price.Len() > 10 ? 88.f : 66.f;
			const float TextWidth = W > 0.f ? FMath::Max(80.f, W - 22.f - 54.f - 8.f - Right) : 0.f;
			return SNew(SPortsButton)
				.Look(Look).HoverLook(Hover)
				.Enabled(bOn)
				.DisabledOpacity(0.45f)
				.HoverShift(FVector2D(3, 0)).DownShift(FVector2D(3, 1))
				.Padding(FMargin(11, 6))
				.OnClicked([this, Id]() { StartAction(Id); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[ PortsUi::Picture(TEXT("action_") + Id, FVector2D(44, 44)) ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ Rich(Name, TEXT("Ports.Body"), ETextJustify::Left, true, TextWidth) ]
						+ SVerticalBox::Slot().AutoHeight()[ Rich(What, TEXT("Ports.Small"), ETextJustify::Left, true, TextWidth) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[
						SNew(SBox).WidthOverride(Right)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ PortsUi::Box(PortsUi::PlainLook(FLinearColor::Transparent, 4, PortsUi::Color(TEXT("#4d3a22")), 1), Rich(Key, TEXT("Ports.Small"), ETextJustify::Left, false), FMargin(7, 0)) ]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Rich(Price, TEXT("Ports.Small"), ETextJustify::Center, false) ]
						]
					]
				];
		}, FMargin(0, 5));
	};
	for (const V& A : Data().Actions().GetItems()) if (!A.Has(TEXT("group"))) ActionButton(A);
	Panel.Add(PortsUi::Box(PortsUi::PlainLook(PortsUi::Color(TEXT("#d8bc7c")), 0), SNew(SBox).HeightOverride(1)), FMargin(0, 7, 0, 4));
	Panel.Add(Rich(TEXT("Merchant's Ledger"), TEXT("Ports.Label"), ETextJustify::Left, false), FMargin(0, 0, 0, 2));
	for (const V& A : Data().Actions().GetItems()) if (A.Get(TEXT("group")).AsString() == TEXT("ledger")) ActionButton(A);
	Panel.Space(6);
	Panel.Add(PortsUi::Button(TEXT("End turn  <lkey>{k:end}</>"), [this]() { TryEndTurn(); }, EButton::Primary));
	Doc.Panel(TEXT("Actions"), PortsUi::Color(TEXT("#1d4a86")), Panel);
}

FString UPortsGameFlow::HintFor(const FPortsPlayer& P) const
{
	using namespace Ports;
	const bool bOn = Ui.hints && (RoundNumber(State) == 1 || State.difficulty == TEXT("apprentice"));
	if (!bOn) return FString();
	if (P.pending.Num()) return TEXT("A card needs your decision first.");
	FString Danger;
	for (const FString& L : FamilyLocations(P)) if (L != ESTATE && (IsStricken(State, L) || IsThreatened(State, L))) { Danger = L; break; }
	if (P.ap == 0) return TEXT("You are out of action points. Press <key>{k:end}</> to end your turn and pass the device.");
	if (!Danger.IsEmpty() && IsStricken(State, Danger))
	{
		const bool bCanMove = P.ap >= ApCost(TEXT("move")) || P.free.Get(TEXT("move"), false) || P.free.Get(TEXT("moveNoPenalty"), false);
		const FString MoveText = bCanMove ? FString::Printf(TEXT("<key>{k:move}</> Move Family (%d AP; fleeing costs %d reputation) or "), ApCost(TEXT("move")), Cfg(TEXT("penalties.fleeReputation"))) : FString();
		return FString::Printf(TEXT("Your family in %s is in a Stricken city. They will roll for survival at the end of the round. Consider %s<key>{k:prepare}</> Prepare Household."), *Esc(CityName(Danger)), *MoveText);
	}
	if (!Danger.IsEmpty()) return FString::Printf(TEXT("%s is next to a Stricken city (amber ring). The plague may arrive soon."), *Esc(CityName(Danger)));
	if (P.shipped.Num() == 0) return FString::Printf(TEXT("Start with <key>{k:ship}</> Ship Goods: pick a route from one of your trading posts. Sea routes pay more. Roll a %d on the profit die and you draw a Fortune card!"), Cfg(TEXT("fortune.drawOnProfitDie")));
	const int32 PostCost = Cost(State, TEXT("openPost"), &P);
	if (P.posts.Num() < 2 && P.florins >= PostCost && P.ap >= ApCost(TEXT("post"))) return FString::Printf(TEXT("A second trading post (<key>{k:post}</>, %dƒ and %d AP) lets you ship from two places, and opening it draws a Fortune card."), PostCost, ApCost(TEXT("post")));
	const FString* After = P.posts.FindByPredicate([&](const FString& C) { return IsAftermath(State, C); });
	if (After && P.land.Num() == 0 && FamilyTotal(P) < Cfg(TEXT("start.family"))) return FString::Printf(TEXT("%s is in Aftermath: you can now <key>{k:marry}</> Arrange a Marriage or <key>{k:land}</> Buy Abandoned Land there."), *Esc(CityName(*After)));
	if (!Ui.history) return TEXT("Tip: your weakest Legacy category counts twice, so keep all three healthy.");
	return TEXT("Tip: click any city on the map to read its history. Your weakest Legacy category counts twice, so keep all three healthy.");
}

// ---------- Choosing an action ----------

void UPortsGameFlow::OpenActionPrompt(const FString& Id)
{
	using namespace Ports;
	if (Id == TEXT("move")) { OpenMovePrompt(); return; }
	const FPortsPlayer& P = *CurrentPlayer(State);
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	// The buttons close the choice with a value; parse turns it into the action.
	struct FItem { FString Value, Main, Sub, Why; };
	TArray<FItem> Items;
	TArray<FString> Notes;
	FString Confirm;
	Selectable.Reset();

	if (Id == TEXT("ship"))
	{
		bool bAnyOffshore = false;
		for (const FString& From : P.posts) for (const int32 Index : Data().RoutesFrom(From))
		{
			const FPortsRoute& R = Data().Routes[Index];
			const FPortsAction Action = ShipAction(From, R.Id);
			const FString Why = CheckAction(State, Action);
			const FPortsShipQuote Q = ShipQuote(State, P, R.Id, From);
			const FString Risk = Q.safe ? FString(TEXT("Clean hold: no contagion risk (Fortune card)"))
				: Q.contagionRisk ? FString::Printf(TEXT("<srisk>Contagion: infected on a roll of %d or less (%d%%)</>"), Q.contagionRisk, FMath::RoundToInt32(Q.contagionRisk / 6.0 * 100))
				: FString(TEXT("No contagion risk (origin not Stricken)"));
			const FString& DestState = State.City(Q.to)->state;
			const TCHAR* Dest = DestState == TEXT("stricken") ? TEXT(" · destination Stricken") : DestState == TEXT("aftermath") ? TEXT(" · destination in Aftermath (+prices)") : TEXT("");
			Items.Add({ From + TEXT("|") + R.Id, FString::Printf(TEXT("%s → %s <small>(%s, value %d)</>"), *Esc(CityName(From)), *Esc(CityName(Q.to)), *R.Type, R.Value),
				FString::Printf(TEXT("Earn %d–%dƒ%s · %s"), Q.min, Q.max, Dest, *Risk), Why });
			if (Q.contagionRisk && Why.IsEmpty() && CheckAction(State, ShipAction(From, R.Id, true)).IsEmpty())
			{
				bAnyOffshore = true;
				Items.Add({ From + TEXT("|") + R.Id + TEXT("|offshore"), FString::Printf(TEXT("…and hold the ship offshore <small>(+%dƒ)</>"), Cost(State, TEXT("holdOffshore"))),
					TEXT("If the cargo is infected: still half profit, but no reputation lost and the plague does not spread"), FString() });
			}
		}
		const bool bFamilyBonus = P.posts.ContainsByPredicate([&P](const FString& C) { return FamilyAt(P, C) > 0; });
		Doc->H2(TEXT("Ship Goods"));
		Doc->P(FString::Printf(TEXT("Choose a route from one of your trading posts. Earnings = route value + profit die%s. A profit die of %d draws a Fortune card."), bFamilyBonus ? TEXT(" + family bonus where your family lives") : TEXT(""), Cfg(TEXT("fortune.drawOnProfitDie"))));
		if (bAnyOffshore) Notes = { TEXT("ME-12"), TEXT("ME-14") };
	}
	else if (Id == TEXT("post"))
	{
		TArray<FString> Seen;
		for (const FString& Own : P.posts) for (const FString& N : Neighbors(Own))
		{
			if (Seen.Contains(N) || P.posts.Contains(N)) continue;
			Seen.Add(N);
			const FString Why = CheckAction(State, CityAction(TEXT("post"), N));
			int32 Best = 0;
			for (const int32 Index : Data().RoutesFrom(N)) Best = FMath::Max(Best, Data().Routes[Index].Value);
			Items.Add({ N, Esc(CityName(N)), FString::Printf(TEXT("%s · %d routes (best value %d)"), *StatusOf(State, N), Data().RoutesFrom(N).Num(), Best), Why });
			if (Why.IsEmpty()) Selectable.Add(N);
		}
		Doc->H2(FString::Printf(TEXT("Open Trading Post (%s)"), P.free.Get(TEXT("post"), false) ? TEXT("free") : *FString::Printf(TEXT("%dƒ"), Cost(State, TEXT("openPost"), &P))));
		Doc->P(TEXT("Choose a city connected by a route to one of your posts (glowing on the map). New posts let you ship from more places, and opening one draws a Fortune card."));
		if (Items.Num() == 0) Doc->P(TEXT("No connected cities."));
	}
	else if (Id == TEXT("prepare") || Id == TEXT("physician"))
	{
		const bool bPrepare = Id == TEXT("prepare");
		const bool bFreeNow = P.free.Get(Id, false);
		const FString Price = bFreeNow ? FString(TEXT("free")) : FString::Printf(TEXT("%dƒ"), Cost(State, bPrepare ? TEXT("prepareHousehold") : TEXT("physician")));
		Doc->H2(FString::Printf(TEXT("%s (%s)"), bPrepare ? TEXT("Prepare Household") : TEXT("Consult Physician"), *Price));
		Doc->P(bPrepare
			? FString::Printf(TEXT("Your household shuts its doors and stockpiles food. This round, survival rolls there get +%d."), Cfg(TEXT("plague.prepareBonus")))
			: FString::Printf(TEXT("Medieval remedies could not cure the plague. Nursing care gives a slim chance: the first family member there who would die this round rolls again and survives on a %d."), Cfg(TEXT("plague.physicianSaveOn"))));
		for (const FString& L : FamilyLocations(P))
		{
			if (L == ESTATE) continue;
			const FPortsCityState& C = *State.City(L);
			const FString Status = C.state == TEXT("stricken") ? FString::Printf(TEXT("<srisk>Stricken (%s)</>"), *SeverityName(C.severity)) : IsThreatened(State, L) ? FString(TEXT("Threatened")) : C.state == TEXT("aftermath") ? FString(TEXT("Aftermath (plague has passed)")) : FString(TEXT("Safe"));
			Items.Add({ L, FString::Printf(TEXT("%s: %d family"), *Esc(CityName(L)), FamilyAt(P, L)), Status, CheckAction(State, CityAction(Id, L)) });
		}
		Notes = ActionInfo(Id).Get(TEXT("factIds")).ToStrings();
	}
	else if (Id == TEXT("marry") || Id == TEXT("land") || Id == TEXT("gates"))
	{
		if (Id == TEXT("marry"))
		{
			Doc->H2(FString::Printf(TEXT("Arrange a Marriage (%dƒ)"), Cost(State, TEXT("marriage"))));
			Doc->P(FString::Printf(TEXT("With the epidemic over, survivors married and many children were born. Choose a city in Aftermath where your family lives: +%d family member there. Your house cannot grow beyond %d."), Cfg(TEXT("gains.marriageFamily")), Cfg(TEXT("start.family"))));
		}
		else if (Id == TEXT("land"))
		{
			Doc->H2(FString::Printf(TEXT("Buy Abandoned Land (%dƒ)"), Cost(State, TEXT("buyLand"))));
			Doc->P(FString::Printf(TEXT("So many farmers died that fields lay empty. Land near a city in Aftermath is worth <b>%d Wealth points</> at the end, but workers were scarce and wages high: you pay %dƒ per holding every half-year (or lose 1 reputation if you cannot)."), Cfg(TEXT("scoring.pointsPerLand")), Cfg(TEXT("costs.landWage"))));
		}
		else
		{
			Doc->H2(FString::Printf(TEXT("Close Your Gates (−%d reputation)"), Cfg(TEXT("penalties.gatesReputation"))));
			Doc->P(FString::Printf(TEXT("Frightened towns posted guards and turned strangers away. Until the end of next round, rival houses cannot open a trading post in the city you choose, and their shipments to it earn %dƒ less. Choose a city where you have a post and family."), Cfg(TEXT("penalties.gatesProfit"))));
		}
		for (const FString& C : P.posts)
		{
			const FString Why = CheckAction(State, CityAction(Id, C));
			TArray<FString> Rivals;
			for (const FPortsPlayer& O : State.players) if (&O != &P && O.posts.Contains(C)) Rivals.Add(Esc(O.name));
			Items.Add({ C, Esc(CityName(C)), FString::Printf(TEXT("%s · %d family%s"), *StatusOf(State, C), FamilyAt(P, C), Id == TEXT("gates") && Rivals.Num() ? *FString::Printf(TEXT(" · rival posts: %s"), *FString::Join(Rivals, TEXT(", "))) : TEXT("")), Why });
			if (Why.IsEmpty()) Selectable.Add(C);
		}
		Notes = ActionInfo(Id).Get(TEXT("factIds")).ToStrings();
	}
	else if (Id == TEXT("loan"))
	{
		Doc->H2(TEXT("Take a Loan"));
		Doc->P(FString::Printf(TEXT("Florence's great banks had collapsed just before the plague, so lenders were careful. A banker will lend you <b>%dƒ</> now. You must repay <b>%dƒ</> in the plague phase of the next round (%s)."), Cfg(TEXT("gains.loan")), Cfg(TEXT("costs.loanRepay")), *Esc(UntilLabel(UntilRound(State, 2)))));
		Doc->P(FString::Printf(TEXT("If you cannot pay in full, you pay everything you have and lose <b>%d reputation</>. Taking a loan costs no action point."), Cfg(TEXT("penalties.loanDefaultReputation"))));
		Notes = ActionInfo(Id).Get(TEXT("factIds")).ToStrings();
		Confirm = FString::Printf(TEXT("Borrow %dƒ"), Cfg(TEXT("gains.loan")));
	}
	else if (Id == TEXT("deal"))
	{
		Doc->H2(TEXT("Propose a Partnership"));
		Doc->P(FString::Printf(TEXT("Merchant ships linked the Italian cities with the Hanseatic League of the north. Offer another house a partnership until the end of next round: when either of you ships to a city where the other has a trading post, <b>both earn %dƒ more</>."), Cfg(TEXT("gains.dealBonus"))));
		Doc->P(TEXT("They will accept or decline at the start of their next turn. Proposing costs no action point."));
		for (const FPortsPlayer& O : State.players)
		{
			if (&O == &P) continue;
			FPortsAction A; A.type = TEXT("deal"); A.partner = O.id;
			TArray<FString> Posts;
			for (const FString& C : O.posts) Posts.Add(Esc(CityName(C)));
			Items.Add({ FString::FromInt(O.id), FString::Printf(TEXT("%s %s"), *PortsUi::CrestGlyph(O.crest), *Esc(O.name)), FString::Printf(TEXT("Posts: %s"), *FString::Join(Posts, TEXT(", "))), CheckAction(State, A) });
		}
		Notes = ActionInfo(Id).Get(TEXT("factIds")).ToStrings();
	}
	else if (Id == TEXT("charity"))
	{
		const int32 Price = CharityCost(State, P);
		const int32 Gain = FMath::Max(1, Cfg(TEXT("gains.charityReputation")) + State.effects.charityBonus);
		Doc->H2(TEXT("Charity & Piety"));
		Doc->P(FString::Printf(TEXT("Support your city in its hour of need.%s"), LastPlaceId(State) == P.id ? TEXT(" (Your house is in last place, so it costs 1ƒ less.)") : TEXT("")));
		struct FKind { const TCHAR* Id; const TCHAR* Label; };
		for (const FKind& K : { FKind{ TEXT("hospital"), TEXT("Fund a hospital") }, FKind{ TEXT("confraternity"), TEXT("Endow a confraternity") }, FKind{ TEXT("church"), TEXT("Give to your parish church") } })
		{
			FPortsAction A; A.type = TEXT("charity"); A.kind = K.Id;
			Items.Add({ K.Id, K.Label, FString::Printf(TEXT("%dƒ → +%d reputation"), Price, Gain), CheckAction(State, A) });
		}
		Notes = ActionInfo(Id).Get(TEXT("factIds")).ToStrings();
	}
	else return;

	// Possible choices first.
	Items.StableSort([](const FItem& A, const FItem& B) { return A.Why.IsEmpty() && !B.Why.IsEmpty(); });

	OpenPrompt = Id;
	FPortsDialogOptions Opts;
	Opts.bSide = true;
	if (!Confirm.IsEmpty()) Opts.EnterValue = TEXT("go");
	PromptCityChosen = nullptr;
	Refresh();
	Open([this, Doc, Items, Notes, Confirm, Opts](TFunction<void(const FString&)> Close)
	{
		for (const FItem& Item : Items) Doc->Choice(Item.Main, Item.Sub, Item.Why, [Close, Value = Item.Value]() { Close(Value); });
		Doc->Note(Notes);
		TArray<TSharedRef<SWidget>> Buttons = { PortsUi::Button(TEXT("Cancel  <small>Esc</>"), [Close]() { Close(FString()); }, EButton::Ghost) };
		if (!Confirm.IsEmpty()) Buttons.Add(PortsUi::Button(PortsUi::Esc(Confirm), [Close]() { Close(TEXT("go")); }, EButton::Primary));
		Doc->Buttons(Buttons);
		// A glowing city on the map can be clicked instead of its line in the list.
		PromptCityChosen = [this, Close](const FString& City) { if (Selectable.Contains(City)) Close(City); };
		return Doc->Build(Opts.InnerWidth());
	}, Opts, [this, Id](const FString& Value)
	{
		OpenPrompt.Reset();
		Selectable.Reset();
		PromptCityChosen = nullptr;
		if (Value.IsEmpty()) { Refresh(); return; }
		FPortsAction A;
		A.type = Id;
		if (Id == TEXT("ship"))
		{
			TArray<FString> Parts;
			Value.ParseIntoArray(Parts, TEXT("|"));
			if (Parts.Num() < 2) return;
			A.from = Parts[0]; A.route = Parts[1]; A.offshore = Parts.Num() > 2;
		}
		else if (Id == TEXT("deal")) A.partner = FCString::Atoi(*Value);
		else if (Id == TEXT("charity")) A.kind = Value;
		else if (Id != TEXT("loan")) A.city = Value;
		RunAction(A);
	});
}

void UPortsGameFlow::OpenMovePrompt()
{
	using namespace Ports;
	struct FSel { FString From, To; int32 Count = 1; };
	const FPortsPlayer& P0 = *CurrentPlayer(State);
	const TArray<FString> Froms = FamilyLocations(P0);
	if (Froms.Num() == 0) return;
	const TSharedRef<FSel> Sel = MakeShared<FSel>();
	Sel->From = Froms[0];
	Sel->To = Froms[0] != ESTATE ? FString(ESTATE) : (P0.posts.Num() ? P0.posts[0] : FString());
	const auto MoveAction = [Sel]() { FPortsAction A; A.type = TEXT("move"); A.from = Sel->From; A.to = Sel->To; A.count = Sel->Count; return A; };

	OpenPrompt = TEXT("move");
	FPortsDialogOptions Opts;
	Opts.bSide = true;
	Open([this, Sel, MoveAction, Opts](TFunction<void(const FString&)> Close)
	{
		const TSharedRef<SBox> Holder = SNew(SBox);
		TWeakPtr<SBox> WeakHolder = Holder;
		PromptRebuild = MakeShared<TFunction<void()>>([this, Sel, MoveAction, Close, WeakHolder, Opts]()
		{
			const TSharedPtr<SBox> Box = WeakHolder.Pin();
			const FPortsPlayer* Current = CurrentPlayer(State);
			if (!Box.IsValid() || !Current) return;
			const FPortsPlayer& P = *Current;
			const bool bNoPenalty = P.free.Get(TEXT("moveNoPenalty"), false);
			const auto Again = [this]() { if (PromptRebuild.IsValid()) { const TSharedPtr<TFunction<void()>> Keep = PromptRebuild; (*Keep)(); } };
			FPortsDoc Doc;
			Doc.H2(FString::Printf(TEXT("Move Family %s"), P.free.Get(TEXT("move"), false) || bNoPenalty ? TEXT("(free this time)") : TEXT("")));
			Doc.P(FString::Printf(TEXT("Family can live in any city where you have a trading post, or at your Country Estate in the countryside (safe from the plague, but it earns no family bonus). Leaving a <b>Stricken</> city is fleeing and costs %s."),
				bNoPenalty ? TEXT("no reputation this time (Fortune card)") : *FString::Printf(TEXT("%d reputation"), Cfg(TEXT("penalties.fleeReputation")))));
			Doc.H3(TEXT("From"));
			TArray<TSharedRef<SWidget>> Row;
			for (const FString& L : FamilyLocations(P))
			{
				Row.Add(PortsUi::Button(FString::Printf(TEXT("%s (%d family%s)"), *Esc(CityName(L)), FamilyAt(P, L), L != ESTATE && IsStricken(State, L) ? TEXT(", Stricken") : TEXT("")),
					[Sel, L, Again]() { Sel->From = L; Again(); }, Sel->From == L ? EButton::SmallOn : EButton::Small));
			}
			Doc.Row(Row, 6);
			Doc.H3(TEXT("To"));
			Row.Reset();
			TArray<FString> Places;
			Places.Add(ESTATE);
			Places.Append(P.posts);
			for (const FString& L : Places)
			{
				Row.Add(PortsUi::Button(FString::Printf(TEXT("%s%s"), *Esc(CityName(L)), L != ESTATE && IsStricken(State, L) ? TEXT(" (Stricken!)") : L != ESTATE && IsThreatened(State, L) ? TEXT(" (threatened)") : TEXT("")),
					[Sel, L, Again]() { Sel->To = L; Again(); }, Sel->To == L ? EButton::SmallOn : EButton::Small));
			}
			Doc.Row(Row, 6);
			Doc.H3(TEXT("How many"));
			Row.Reset();
			for (int32 N = 1; N <= Cfg(TEXT("limits.moveFamilyMax")); N++)
			{
				Row.Add(PortsUi::Button(FString::FromInt(N), [Sel, N, Again]() { Sel->Count = N; Again(); }, Sel->Count == N ? EButton::SmallOn : EButton::Small));
			}
			Doc.Row(Row, 6);
			const FString Why = CheckAction(State, MoveAction());
			if (!Why.IsEmpty()) Doc.P(FString::Printf(TEXT("<risk>%s</>"), *Esc(Why)));
			else if (!bNoPenalty && Sel->From != ESTATE && IsStricken(State, Sel->From)) Doc.P(FString::Printf(TEXT("<warn>Fleeing %s will cost %d reputation.</>"), *Esc(CityName(Sel->From)), Cfg(TEXT("penalties.fleeReputation"))));
			Doc.Note({ TEXT("SO-04") });
			Doc.Buttons({
				PortsUi::Button(TEXT("Cancel  <small>Esc</>"), [Close]() { Close(FString()); }, EButton::Ghost),
				PortsUi::Button(TEXT("Move"), [Close]() { Close(TEXT("go")); }, EButton::Primary, Why.IsEmpty()),
			});
			Box->SetContent(Doc.Build(Opts.InnerWidth()));
		});
		(*PromptRebuild)();
		return Holder;
	}, Opts, [this, MoveAction](const FString& Value)
	{
		OpenPrompt.Reset();
		PromptRebuild.Reset();
		if (Value == TEXT("go")) RunAction(MoveAction());
		else Refresh();
	});
}

// ---------- Decisions (offers, protecting the persecuted, wage laws, partnerships) ----------

void UPortsGameFlow::OpenDecisionPrompt()
{
	using namespace Ports;
	const FPortsPlayer& P = *CurrentPlayer(State);
	const V D = P.pending[0];
	const FString Kind = D.Get(TEXT("kind")).AsString();
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	const FString Why = CanAccept(State, P, D);
	struct FButtonSpec { FString Label, Value; bool bPrimary, bEnabled; };
	TArray<FButtonSpec> Buttons;
	FPortsDoc Body;
	FString Theme, Title;
	TArray<FString> Facts;

	if (Kind == TEXT("deal"))
	{
		const FPortsPlayer& From = State.players[D.Get(TEXT("from")).AsInt()];
		TArray<FString> Posts, Shared;
		for (const FString& C : From.posts)
		{
			Posts.Add(Esc(CityName(C)));
			if (P.posts.ContainsByPredicate([&C](const FString& X) { return X == C || Neighbors(X).Contains(C); })) Shared.Add(Esc(CityName(C)));
		}
		Body.P(FString::Printf(TEXT("%s <b>%s</> of %s proposes a partnership until the end of next round."), *PortsUi::CrestGlyph(From.crest), *Esc(From.name), *Esc(CityName(From.home))));
		Body.P(FString::Printf(TEXT("When either house ships to a city where the other has a trading post, <b>both earn %dƒ more</>. Their posts: %s.%s"), Cfg(TEXT("gains.dealBonus")), *FString::Join(Posts, TEXT(", ")),
			Shared.Num() ? *FString::Printf(TEXT(" Your routes reach %s."), *FString::Join(Shared, TEXT(", "))) : TEXT("")));
		Theme = TEXT("trade");
		Title = FString::Printf(TEXT("A Partnership with %s"), *From.name);
		Facts = { TEXT("TR-04") };
		Buttons.Add({ TEXT("Accept the partnership"), TEXT("yes"), true, Why.IsEmpty() });
		Buttons.Add({ TEXT("Decline"), TEXT("no"), false, true });
	}
	else
	{
		const V& Card = CardById(D.Get(TEXT("card")).AsString());
		Theme = D.Get(TEXT("fortune")).Truthy() ? FString(TEXT("fortune")) : Card.Get(TEXT("theme")).AsString();
		Title = Card.Get(TEXT("title")).AsString();
		Facts = Card.Get(TEXT("factIds")).ToStrings();
		if (Kind == TEXT("offer"))
		{
			const V& Gain = D.Get(TEXT("gain"));
			TArray<FString> Gains, Penalties;
			if (Gain.Get(TEXT("reputation")).Truthy()) Gains.Add(FString::Printf(TEXT("+%d reputation"), Gain.Get(TEXT("reputation")).AsInt()));
			if (Gain.Get(TEXT("florins")).Truthy()) Gains.Add(FString::Printf(TEXT("+%dƒ"), Gain.Get(TEXT("florins")).AsInt()));
			const V& Pen = D.Get(TEXT("declinePenalty"));
			if (Pen.Get(TEXT("reputation")).Truthy()) Penalties.Add(FString::Printf(TEXT("−%d reputation"), Pen.Get(TEXT("reputation")).AsInt()));
			if (Pen.Get(TEXT("florins")).Truthy()) Penalties.Add(FString::Printf(TEXT("−%dƒ"), Pen.Get(TEXT("florins")).AsInt()));
			if (Pen.Get(TEXT("blockHome")).Truthy()) Penalties.Add(TEXT("home post cannot ship"));
			Body.P(Esc(Card.Get(TEXT("text")).AsString()));
			Buttons.Add({ FString::Printf(TEXT("%s (%dƒ%s)"), *Esc(D.Get(TEXT("label")).AsString()), D.Get(TEXT("cost")).Get(TEXT("florins")).AsInt(0), Gains.Num() ? *FString::Printf(TEXT(" → %s"), *FString::Join(Gains, TEXT(", "))) : TEXT("")), TEXT("yes"), false, Why.IsEmpty() });
			Buttons.Add({ FString::Printf(TEXT("%s%s"), *Esc(D.Get(TEXT("decline")).AsString()), Penalties.Num() ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Penalties, TEXT(", "))) : TEXT("")), TEXT("no"), true, true });
		}
		else if (Kind == TEXT("protect"))
		{
			Body.P(FString::Printf(TEXT("The Jewish community of %s has been falsely accused of causing the plague. <b>The accusations were false, and the violence was unjust.</>"), *Esc(CityName(D.Get(TEXT("city")).AsString()))));
			Body.P(FString::Printf(TEXT("Your house can use its money and influence to shelter and defend the community. It will cost %dƒ and 1 action point from this turn, and earns %d reputation. Nothing can be gained from persecution."), Cfg(TEXT("costs.protectCommunity")), Cfg(TEXT("gains.protectReputation"))));
			Buttons.Add({ FString::Printf(TEXT("Protect the community (%dƒ, 1 AP)"), Cfg(TEXT("costs.protectCommunity"))), TEXT("yes"), true, Why.IsEmpty() });
			Buttons.Add({ TEXT("Do not intervene"), TEXT("no"), false, true });
		}
		else if (Kind == TEXT("wageLaw"))
		{
			TArray<FString> Mine;
			for (const FString& C : D.Get(TEXT("cities")).ToStrings()) if (P.posts.Contains(C)) Mine.Add(Esc(CityName(C)));
			Body.P(Esc(Card.Get(TEXT("text")).AsString()));
			Body.P(FString::Printf(TEXT("You own a post in %s."), *FString::Join(Mine, TEXT(" and "))));
			Body.Bullets({
				FString::Printf(TEXT("<b>Obey</>: +%d reputation, but your English posts cannot ship this round (workers refuse the old wages)."), Cfg(TEXT("wageLaw.obeyReputation"))),
				FString::Printf(TEXT("<b>Pay market wages</>: roll a die; on 1–%d you are fined %dƒ."), Cfg(TEXT("wageLaw.fineMaxRoll")), Cfg(TEXT("wageLaw.fine"))),
			});
			Buttons.Add({ TEXT("Obey the law"), TEXT("obey"), false, true });
			Buttons.Add({ TEXT("Pay market wages"), TEXT("pay"), true, true });
		}
	}
	Body.Note(Facts);
	Doc->Card(Theme, FString::Printf(TEXT("Decision for %s"), *Esc(P.name)), Title, Body);
	if (!Why.IsEmpty() && Kind != TEXT("wageLaw")) Doc->P(FString::Printf(TEXT("<risk>%s</>"), *Esc(Why)));

	FPortsDialogOptions Opts;
	Opts.bDismissable = false;
	Open([Doc, Buttons, Opts](TFunction<void(const FString&)> Close)
	{
		TArray<TSharedRef<SWidget>> Row;
		for (const FButtonSpec& B : Buttons) Row.Add(PortsUi::Button(B.Label, [Close, Value = B.Value]() { Close(Value); }, B.bPrimary ? EButton::Primary : EButton::Normal, B.bEnabled));
		Doc->Buttons(Row);
		return Doc->Build(Opts.InnerWidth());
	}, Opts, [this](const FString& Value)
	{
		ApplyDecision(Value == TEXT("obey") ? EPortsChoice::Obey : Value == TEXT("pay") ? EPortsChoice::Pay : Value == TEXT("yes") ? EPortsChoice::Yes : EPortsChoice::No);
	});
}

// Asked only when action points are left.
void UPortsGameFlow::OpenEndTurnPrompt()
{
	const FPortsPlayer& P = *Ports::CurrentPlayer(State);
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	Doc->H2(TEXT("End your turn?"));
	Doc->P(FString::Printf(TEXT("You still have %d action point%s. Unused points are lost."), P.ap, P.ap > 1 ? TEXT("s") : TEXT("")));
	FPortsDialogOptions Opts;
	Opts.EnterValue = TEXT("end");
	const int32 Id = P.id;
	Open([Doc, Opts](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({
			PortsUi::Button(TEXT("Keep playing"), [Close]() { Close(FString()); }),
			PortsUi::Button(TEXT("End turn  <lsmall>Enter</>"), [Close]() { Close(TEXT("end")); }, EButton::Primary),
		});
		return Doc->Build(Opts.InnerWidth());
	}, Opts, [this, Id](const FString& Value)
	{
		const FPortsPlayer* Now = Ports::CurrentPlayer(State);
		if (Value == TEXT("end") && Now && Now->id == Id) FinishTurn();
	});
}
