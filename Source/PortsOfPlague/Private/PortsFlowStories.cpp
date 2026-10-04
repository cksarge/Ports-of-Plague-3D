// The story cards: prologue, turn order, round start, Chronicle and Event
// cards, Fortune cards, plague results, shipment results and the like. Each is
// built from a kind and its log entries alone, as in src/ui/stories.js.
#include "PortsGameFlow.h"

#include "PortsUi.h"
#include "SPortsRoot.h"
#include "Widgets/Layout/SBox.h"

using V = FPortsValue;
using PortsUi::Esc;
using PortsUi::Rich;

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

	TArray<FString> Ids(const V& List) { return List.ToStrings(); }

	FString PageOf(int32 Page, int32 Pages) { return Pages > 1 ? FString::Printf(TEXT(" <small>(%d of %d)</>"), Page, Pages) : FString(); }

	FString YearOf(const FString& Label)
	{
		TArray<FString> Parts;
		Label.ParseIntoArray(Parts, TEXT(" "));
		return Parts.IsValidIndex(1) ? Parts[1] : Label;
	}

	// "1–2 Light, 3–4 Heavy, 5–6 Devastating", from the table in config.json.
	FString SeverityBands()
	{
		TArray<FString> Out;
		for (int32 Sev = 1; Sev <= Cfg(TEXT("plague.severityMax")); Sev++)
		{
			int32 Low = 0, High = 0;
			for (int32 Die = 1; Die <= 6; Die++)
			{
				if (Cfg(*FString::Printf(TEXT("plague.severityTable.%d"), Die)) != Sev) continue;
				if (!Low) Low = Die;
				High = Die;
			}
			if (Low) Out.Add(FString::Printf(TEXT("%d–%d %s"), Low, High, *Ports::SeverityName(Sev)));
		}
		return FString::Join(Out, TEXT(", "));
	}

	// One struck city in the dice tray: its name and date on the left, its severity roll on the right.
	FPortsDoc::FMake ArrivalRow(const FPortsState& State, const V& A)
	{
		const FPortsCity& City = *Data().FindCity(A.Get(TEXT("city")).AsString());
		FPortsDoc Left;
		Left.Text(FString::Printf(TEXT("<lcaps>%s</>"), *Esc(City.Name.ToUpper())), TEXT("Ports.Light"), ETextJustify::Left, FMargin(0));
		if (A.Get(TEXT("type")).AsString() != TEXT("arrival"))
		{
			Left.Text(Esc(A.Get(TEXT("text")).AsString()), TEXT("Ports.LightSmall"), ETextJustify::Left, FMargin(0));
			return [Left](float W) { return Left.Build(W); };
		}
		Left.Text(FString::Printf(TEXT("%sHistorically: %s"), A.Get(TEXT("early")).Truthy() ? TEXT("Brought early by infected cargo. ") : TEXT(""), *Esc(City.ArrivalDateText)), TEXT("Ports.LightSmall"), ETextJustify::Left, FMargin(0));
		const int32 Die = A.Get(TEXT("die")).AsInt(), Severity = A.Get(TEXT("severity")).AsInt();
		const int32 Base = Cfg(*FString::Printf(TEXT("plague.severityTable.%d"), Die));
		TArray<FString> Mods;
		const int32 CityMod = City.SeverityMod;
		const int32 DiffMod = Cfg(*(Ports::DifficultyPath(State) + TEXT(".severityMod")));
		if (CityMod) Mods.Add(FString::Printf(TEXT("%s%d %s"), CityMod > 0 ? TEXT("+") : TEXT(""), CityMod, CityMod > 0 ? TEXT("hard-hit region") : TEXT("lighter region")));
		if (DiffMod) Mods.Add(FString::Printf(TEXT("%s%d difficulty"), DiffMod > 0 ? TEXT("+") : TEXT(""), DiffMod));
		FString Pips;
		for (int32 i = 0; i < Severity; i++) Pips += TEXT("\u25CF");
		const FString Caption = FString::Printf(TEXT("Severity roll: %d%s \u2192 <lb>%s</> %s"), Die,
			Mods.Num() && Base != Severity ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Mods, TEXT(", "))) : TEXT(""), *Esc(Ports::SeverityName(Severity)), *Pips);
		return [Left, Die, Caption](float W)
		{
			// The die takes a fixed room in the middle; the two texts share the rest.
			const float Rest = W > 0.f ? FMath::Max(80.f, W - 62.f) : 0.f;
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Left.Build(Rest * 0.52f) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0)[ PortsUi::Die(Die, true, false, true) ]
				+ SHorizontalBox::Slot().FillWidth(0.92f).VAlign(VAlign_Center)[ Rich(Caption, TEXT("Ports.LightSmall"), ETextJustify::Left, true, Rest * 0.48f) ];
		};
	}

	// One Chronicle or Event card, with what it did.
	void AddGameCard(FPortsDoc& Doc, const FPortsState& State, const V& Group, bool bInChronicle)
	{
		const V& E = Group[0];
		const V& Card = Ports::CardById(E.Get(TEXT("card")).AsString());
		const FString Theme = Card.Get(TEXT("theme")).AsString();
		FString Kind;
		if (bInChronicle) Kind = FString::Printf(TEXT("Chronicle · %s"), *Esc(Ports::HalfInfo(Card.Get(TEXT("round")).AsInt()).Get(TEXT("label")).AsString()));
		else
		{
			const FString Base = E.Get(TEXT("deck")).AsString() == TEXT("chronicle")
				? FString::Printf(TEXT("Chronicle · %s"), *Ports::HalfInfo(Card.Has(TEXT("round")) ? Card.Get(TEXT("round")).AsInt() : State.round).Get(TEXT("label")).AsString())
				: FString(TEXT("Event card"));
			Kind = FString::Printf(TEXT("%s · %s"), *Esc(Base), *Esc(PortsUi::Theme(Theme).Label));
		}
		FPortsDoc Body;
		Body.P(Esc(Card.Get(TEXT("text")).AsString()));
		TArray<FString> Effects;
		for (int32 i = 1; i < Group.Num(); i++) Effects.Add(Esc(Group[i].Get(TEXT("text")).AsString()));
		if (Effects.Num()) Body.Bullets(Effects);
		const FString EffectType = Card.Get(TEXT("effect")).Get(TEXT("type")).AsString();
		if (EffectType == TEXT("offer") || EffectType == TEXT("persecution") || EffectType == TEXT("wageLaw")) Body.P(TEXT("<b>Each house decides at the start of its own turn.</>"));
		if (!bInChronicle) Body.Note(Ids(Card.Get(TEXT("factIds"))));
		Doc.Card(Theme, Kind, Card.Get(TEXT("title")).AsString(), Body);
	}
}

void UPortsGameFlow::BuildStory(const FString& Kind, const V& D, FPortsDoc& Doc, FString& Button, FPortsDialogOptions& Opts) const
{
	PortsUi::SetDiceStill(bAutoPlay && TestHold.IsEmpty());
	PortsUi::ResetDice();
	Button = TEXT("Continue");
	Opts.bDismissable = false;
	Opts.bStory = true;
	Opts.EnterValue = TEXT("ok");

	if (Kind == TEXT("prologue"))
	{
		const V& E = D.Get(TEXT("e"));
		const FString Year = YearOf(Ports::HalfInfo(State.firstHalf).Get(TEXT("label")).AsString());
		FString Text = E.Get(TEXT("text")).AsString() + TEXT(" ");
		Text += State.preRounds
			? FString::Printf(TEXT("The game begins in %s, before the plague sails west: use the %s to open trading posts while the ports are safe. The plague years begin in the second half of 1347."), *Year,
				State.preRounds > 1 ? *FString::Printf(TEXT("%d pre-plague rounds"), State.preRounds) : TEXT("pre-plague round"))
			: FString(TEXT("The game begins in the second half of 1347, as Italian ships carry the sickness west."));
		Text += State.mode == TEXT("quick") ? TEXT(" In Quick Play each round of the plague years is a year and a half.") : TEXT(" Each round is half a year.");
		Text += TEXT(" The plague will reach each city on the map when it really did, unless your ships bring it sooner.");
		FPortsDoc Body;
		Body.P(Esc(Text)).Note(Ids(E.Get(TEXT("factIds"))));
		Doc.Card(TEXT("trade"), TEXT("Prologue · 1346"), TEXT("The Siege of Caffa"), Body);
		if (Ui.hints)
		{
			FPortsDoc How;
			How.P(TEXT("<b>How to play in one minute</>"));
			How.Bullets({
				FString::Printf(TEXT("<b>Each round</>, the plague reaches new cities (the dates are real), and Chronicle and Event cards are read aloud.%s"),
					State.turnSeconds ? *FString::Printf(TEXT(" Each turn has a <b>%d-second timer</> (it stops while cards are shown)."), State.turnSeconds) : TEXT("")),
				FString::Printf(TEXT("<b>On your turn</> you have %d action points. Most actions take 1; opening a trading post or moving family takes %d. Press <key>1</> Ship Goods to earn florins; sea routes pay more, but cargo from a Stricken city may be infected."),
					Cfg(*(Ports::ModePath(State) + TEXT(".actionPoints"))), Cfg(TEXT("actionPointCosts.post"))),
				FString::Printf(TEXT("<b>Fortune cards:</> roll a %d when shipping, or open a new trading post, and you draw a personal Fortune card."), Cfg(TEXT("fortune.drawOnProfitDie"))),
				TEXT("<b>Protect your family:</> family in a Stricken city rolls for survival at the end of the round. Move them away (<key>3</>) or prepare your household (<key>4</>)."),
				TEXT("<b>Win</> with the highest Legacy in 1353: Wealth + Family + Reputation, plus your weakest one again. Balance beats greed."),
			}, true);
			Doc.Boxed(PortsUi::PlainLook(PortsUi::Color(TEXT("#eef3fb")), 12, PortsUi::Color(TEXT("#1d4a86")), 2), How, FMargin(14, 8), FMargin(0, 14, 0, 2));
		}
		Button = TEXT("Roll for turn order");
	}
	else if (Kind == TEXT("order"))
	{
		// Each house rolls a die; the highest goes first (ties roll again).
		const V& E = D.Get(TEXT("e"));
		Doc.H2(TEXT("Rolling for Turn Order"), ETextJustify::Center);
		Doc.P(TEXT("The highest roll goes first; tied houses roll again. <b>This order stays the same for the whole game.</>"), ETextJustify::Center);
		const V& Rounds = E.Get(TEXT("rolls"));
		for (int32 R = 0; R < Rounds.Num(); R++)
		{
			// One tray is thrown after another, as the houses roll in turn.
			PortsUi::NextDiceTray();
			FPortsDoc Tray;
			Tray.Text(R == 0 ? TEXT("EVERY HOUSE ROLLS") : TEXT("TIE! THESE HOUSES ROLL AGAIN"), TEXT("Ports.CardKind"), ETextJustify::Left, FMargin(0, 0, 0, 4));
			TArray<TSharedRef<SWidget>> Dice;
			for (const V& X : Rounds[R].GetItems())
			{
				const FPortsPlayer& P = State.players[X.Get(TEXT("player")).AsInt()];
				Dice.Add(PortsUi::Die(X.Get(TEXT("die")).AsInt(), false, true, false, FString::Printf(TEXT("%s %s"), *PortsUi::CrestGlyph(P.crest), *Esc(P.name))));
			}
			Tray.Row(Dice, 22, HAlign_Center);
			Doc.Tray(Tray);
		}
		TArray<TSharedRef<SWidget>> Order;
		const TCHAR* Places[] = { TEXT("1st"), TEXT("2nd"), TEXT("3rd"), TEXT("4th"), TEXT("5th"), TEXT("6th") };
		const TArray<int32> OrderIds = E.Get(TEXT("order")).ToInts();
		for (int32 i = 0; i < OrderIds.Num(); i++)
		{
			const FPortsPlayer& P = State.players[OrderIds[i]];
			FPortsDoc Cell;
			Cell.Width(138);
			Cell.Text(FString::Printf(TEXT("<capsred>%s</>"), Places[FMath::Min(i, 5)]), TEXT("Ports.Body"), ETextJustify::Center, FMargin(0));
			Cell.Add(SNew(SBox).HAlign(HAlign_Center)[ PortsUi::Banner(P.crest, 64) ], FMargin(0, 4));
			Cell.Text(FString::Printf(TEXT("<b>%s</>"), *Esc(P.name)), TEXT("Ports.Body"), ETextJustify::Center, FMargin(0));
			Cell.Text(Esc(CityName(P.home)), TEXT("Ports.Small"), ETextJustify::Center, FMargin(0));
			// .order-card: pale, edged in the house's colour; the first to play has a gold glow.
			FPortsBoxLook Look = PortsUi::PlainLook(PortsUi::Color(TEXT("#fff8e2")), 14, PortsUi::Color(*P.color), 3);
			if (i == 0) { Look.Inset = PortsUi::Color(TEXT("#f3d27a")); Look.InsetAt = 3; Look.InsetWidth = 3; }
			Order.Add(SNew(SBox).WidthOverride(160)[ PortsUi::Box(Look, Cell.Widget(), FMargin(11)) ]);
		}
		// The order is shown only once the last die has come to rest (as the web version does).
		FPortsDoc Result;
		Result.Row(Order, 10, HAlign_Center);
		const float After = PortsUi::DiceSettleTime() + 0.15f;
		Doc.AddBuilt([Result, After](float W) { return PortsUi::Entrance(Result.Build(W), 0, After); });
		Button = State.preRounds ? FString::Printf(TEXT("Begin the year %s"), *YearOf(Ports::HalfInfo(State.firstHalf).Get(TEXT("label")).AsString())) : FString(TEXT("Begin the year 1347"));
		Opts.bWide = true;
	}
	else if (Kind == TEXT("round"))
	{
		const V& Group = D.Get(TEXT("group"));
		const V& Head = Group[0];
		const FPortsRoundInfo Info = Ports::RoundInfo(State);
		const V& Half = Ports::HalfInfo(State.round);
		const FString Years = State.round == State.roundEnd ? Half.Get(TEXT("label")).AsString() : Info.label;
		FPortsDoc Banner;
		Banner.Text(FString::Printf(TEXT("%sROUND %d OF %d"), Info.pre ? TEXT("BEFORE THE PLAGUE · ") : TEXT(""), Ports::RoundNumber(State), Ports::TotalRounds(State)), TEXT("Ports.Small"), ETextJustify::Center, FMargin(0));
		Banner.Text(Esc(Years), TEXT("Ports.Year"), ETextJustify::Center, FMargin(0));
		Banner.Text(FString::Printf(TEXT("<i>%s</>  %s"), *Esc(Info.months), Half.Get(TEXT("season")).AsString() == TEXT("warm") ? TEXT("☀") : TEXT("❄")), TEXT("Ports.Body"), ETextJustify::Center, FMargin(0));
		Banner.P(Esc(Info.headline), ETextJustify::Center);
		Doc.Nest(Banner);
		TArray<FString> Facts = Ids(Head.Get(TEXT("factIds")));
		if (Info.pre)
		{
			Doc.P(FString::Printf(TEXT("<b>Before the plague.</> Only Caffa and Tana on the Black Sea are Stricken. No Event card and no survival rolls this round, and trading posts cost %dƒ less: set up your trade while the ports are safe."), Cfg(TEXT("prePlague.postDiscount"))));
		}
		else if (Group.Num() > 1)
		{
			Doc.H3(TEXT("The plague arrives"));
			const int32 DiffMod = Cfg(*(Ports::DifficultyPath(State) + TEXT(".severityMod")));
			const FString DiffText = DiffMod ? FString::Printf(TEXT(" On %s difficulty every roll counts %s."), *Data().Config().Get(TEXT("difficulty")).Get(State.difficulty).Get(TEXT("label")).AsString(), DiffMod > 0 ? TEXT("1 higher") : TEXT("1 lower")) : FString();
			Doc.Small(FString::Printf(TEXT("Each newly struck city rolls the red <sb>severity die</> to see how badly the plague hits it: %s. Hard-hit Tuscany and Catalonia add 1; Flanders subtracts 1.%s"), *SeverityBands(), *DiffText));
			FPortsDoc Tray;
			for (int32 i = 1; i < Group.Num(); i++)
			{
				Tray.AddBuilt(ArrivalRow(State, Group[i]), FMargin(0, 3));
				Facts.Append(Ids(Group[i].Get(TEXT("factIds"))));
			}
			Doc.Tray(Tray);
		}
		else
		{
			Doc.P(TEXT("No new cities are struck this time."));
		}
		Doc.Note(Facts);
		Opts.bWide = Group.Num() > 5;
	}
	else if (Kind == TEXT("card"))
	{
		AddGameCard(Doc, State, D.Get(TEXT("group")), false);
	}
	else if (Kind == TEXT("chronicle"))
	{
		// Several Chronicle cards of the same round, side by side on one page.
		const V& Groups = D.Get(TEXT("groups"));
		const int32 Page = D.Get(TEXT("page")).AsInt(1), Pages = D.Get(TEXT("pages")).AsInt(1);
		Doc.H2(FString::Printf(TEXT("The Chronicle%s"), *PageOf(Page, Pages)));
		// The cards share the page's width equally: side by side, or two by two when there are four (.chronicle-grid).
		TArray<FPortsDoc> Cards;
		TArray<FString> Facts;
		for (const V& Group : Groups.GetItems())
		{
			FPortsDoc One;
			AddGameCard(One, State, Group, true);
			Cards.Add(One);
			for (const FString& Id : Ids(Ports::CardById(Group[0].Get(TEXT("card")).AsString()).Get(TEXT("factIds")))) Facts.Add(Id);
		}
		const int32 PerRow = Cards.Num() == 4 ? 2 : FMath::Max(1, Cards.Num());
		for (int32 First = 0; First < Cards.Num(); First += PerRow)
		{
			TArray<FPortsDoc> Line;
			for (int32 k = First; k < FMath::Min(Cards.Num(), First + PerRow); k++) Line.Add(Cards[k]);
			Doc.Columns(Line, 14, FMargin(0, 3));
		}
		if (Facts.Num() > 4) Facts.SetNum(4);
		Doc.Note(Facts);
		Button = Page < Pages ? TEXT("More of the chronicle") : TEXT("Continue");
		Opts.bWide = true;
	}
	else if (Kind == TEXT("fortune"))
	{
		const V& E = D.Get(TEXT("e"));
		const V& Card = Ports::FortuneById(E.Get(TEXT("card")).AsString());
		const FPortsPlayer& P = State.players[E.Get(TEXT("player")).AsInt()];
		const FString ToneId = Card.Get(TEXT("tone")).AsString();
		const TCHAR* Tone = ToneId == TEXT("good") ? TEXT("Good fortune") : ToneId == TEXT("bad") ? TEXT("Misfortune") : TEXT("A choice");
		FPortsDoc Body;
		Body.P(Esc(Card.Get(TEXT("text")).AsString()));
		if (E.Get(TEXT("result")).Truthy()) Body.P(FString::Printf(TEXT("<b>%s</>"), *Esc(E.Get(TEXT("result")).AsString())));
		if (E.Get(TEXT("cities")).Num())
		{
			TArray<FString> Names;
			for (const FString& C : Ids(E.Get(TEXT("cities")))) Names.Add(Esc(CityName(C)));
			Body.P(FString::Printf(TEXT("<b>Warning:</> %s."), *FString::Join(Names, TEXT(", "))));
		}
		if (E.Get(TEXT("die")).Truthy())
		{
			FPortsDoc Tray;
			Tray.Row({ PortsUi::Die(E.Get(TEXT("die")).AsInt(), true, false, false, FString::Printf(TEXT("Survival roll (dies on %d or less)"), E.Get(TEXT("severity")).AsInt())) }, 8, HAlign_Center);
			Body.Tray(Tray);
		}
		if (Card.Get(TEXT("effect")).Get(TEXT("type")).AsString() == TEXT("offer")) Body.P(TEXT("<i>You will choose next.</>"));
		Body.Note(Ids(Card.Get(TEXT("factIds"))));
		Doc.Card(TEXT("fortune"), FString::Printf(TEXT("Fortune card · %s %s · %s"), *Esc(P.name), *Esc(E.Get(TEXT("reason")).AsString()), Tone), Card.Get(TEXT("title")).AsString(), Body);
	}
	else if (Kind == TEXT("plague"))
	{
		const V& Group = D.Get(TEXT("group"));
		int32 Halves = 0;
		bool bPre = true, bRolls = false, bDeaths = false;
		for (const V& E : Group.GetItems())
		{
			const FString Type = E.Get(TEXT("type")).AsString();
			if (Type == TEXT("plague")) { Halves++; if (!E.Get(TEXT("pre")).Truthy()) bPre = false; }
			if (Type == TEXT("mortality")) bRolls = true;
			if (E.Get(TEXT("deaths")).AsInt() > 0) bDeaths = true;
		}
		const FString Label = Ports::RoundInfo(State).label;
		if (bPre)
		{
			Doc.H2(FString::Printf(TEXT("The Year Turns: %s"), *Esc(Label)));
			Doc.P(TEXT("No plague yet: only upkeep is paid."));
		}
		else
		{
			Doc.H2(FString::Printf(TEXT("The Plague Takes Its Toll: %s"), *Esc(Label)));
			Doc.P(TEXT("Every family member in a Stricken city rolls the mortality die."));
			const int32 Bonus = Cfg(TEXT("plague.prepareBonus"));
			for (const V& E : Group.GetItems())
			{
				const FString Type = E.Get(TEXT("type")).AsString();
				if (Type == TEXT("plague"))
				{
					if (Halves > 1) Doc.H3(Esc(Ports::HalfInfo(E.Get(TEXT("half")).AsInt()).Get(TEXT("label")).AsString()));
					if (E.Get(TEXT("pre")).Truthy()) Doc.P(Esc(E.Get(TEXT("text")).AsString()));
				}
				else if (Type == TEXT("aftermath")) Doc.P(FString::Printf(TEXT("❦ %s"), *Esc(E.Get(TEXT("text")).AsString())));
				else if (Type != TEXT("mortality")) Doc.P(Esc(E.Get(TEXT("text")).AsString()));
				else
				{
					const FPortsPlayer& P = State.players[E.Get(TEXT("player")).AsInt()];
					const int32 Sev = E.Get(TEXT("severity")).AsInt();
					const bool bPrepared = E.Get(TEXT("prepared")).Truthy();
					FPortsDoc Tray;
					Tray.Light(FString::Printf(TEXT("%s <lb>%s</> in %s (%s): a family member dies if the result is <lb>%d or less</>%s."), *PortsUi::CrestGlyph(P.crest), *Esc(P.name), *Esc(CityName(E.Get(TEXT("city")).AsString())),
						*Esc(Ports::SeverityName(Sev)), Sev, bPrepared ? *FString::Printf(TEXT(" (each roll gets +%d for a prepared household)"), Bonus) : TEXT("")));
					TArray<TSharedRef<SWidget>> Dice;
					for (const V& R : E.Get(TEXT("rolls")).GetItems())
					{
						const bool bDies = R.Get(TEXT("dies")).Truthy();
						const FString Outcome = bDies ? TEXT("died") : R.Get(TEXT("lastHeir")).Truthy() ? TEXT("last heir") : R.Get(TEXT("save")).AsInt() >= Cfg(TEXT("plague.physicianSaveOn")) ? TEXT("nursed back") : TEXT("lived");
						const FString Math = bPrepared ? FString::Printf(TEXT("%d + %d = %d"), R.Get(TEXT("die")).AsInt(), Bonus, R.Get(TEXT("total")).AsInt()) : FString::Printf(TEXT("rolled %d"), R.Get(TEXT("die")).AsInt());
						Dice.Add(PortsUi::Die(R.Get(TEXT("die")).AsInt(), bDies, false, true, FString::Printf(TEXT("%s\n<lb>%s</>"), *Math, *Outcome)));
					}
					Tray.Row(Dice, 14);
					Tray.Light(Esc(E.Get(TEXT("text")).AsString()));
					Doc.Tray(Tray);
				}
			}
			if (!bRolls) Doc.P(TEXT("No family members were in Stricken cities this round."));
			if (bDeaths) Doc.Note({ TEXT("EC-10"), TEXT("DB-01") });
		}
		Button = State.roundEnd >= Cfg(TEXT("rounds")) ? TEXT("Final scoring") : TEXT("Begin the next round");
		Opts.bWide = true;
	}
	else if (Kind == TEXT("ship"))
	{
		const V& E = D.Get(TEXT("e"));
		const int32 ProfitDie = E.Get(TEXT("profitDie")).AsInt();
		const bool bInfected = E.Get(TEXT("infected")).Truthy(), bOffshore = E.Get(TEXT("offshore")).Truthy();
		const FString Spread = E.Get(TEXT("spread")).AsString();
		const FString To = CityName(E.Get(TEXT("to")).AsString());
		Doc.H2(FString::Printf(TEXT("%s → %s"), *Esc(CityName(E.Get(TEXT("from")).AsString())), *Esc(To)));
		const FPortsPlayer& Who = State.players[E.Get(TEXT("player")).AsInt()];
		Doc.P(FString::Printf(TEXT("%s %s ships goods."), *PortsUi::CrestGlyph(Who.crest), *Esc(Who.name)));
		FPortsDoc Tray;
		const bool bFortune = ProfitDie == Cfg(TEXT("fortune.drawOnProfitDie"));
		TArray<TSharedRef<SWidget>> Dice = { PortsUi::Die(ProfitDie, false, bFortune, false, bFortune ? TEXT("Profit die: Fortune!") : TEXT("Profit die")) };
		if (!E.Get(TEXT("contagionDie")).IsNull()) Dice.Add(PortsUi::Die(E.Get(TEXT("contagionDie")).AsInt(), true, false, false, FString::Printf(TEXT("Contagion die (infected on ≤%d)"), E.Get(TEXT("contagionRisk")).AsInt())));
		Tray.Row(Dice, 26, HAlign_Center);
		Doc.Tray(Tray);
		TArray<FString> Parts;
		for (const V& X : E.Get(TEXT("parts")).GetItems()) Parts.Add(FString::Printf(TEXT("%s: %s%d"), *Esc(X.Get(TEXT("label")).AsString()), X.Get(TEXT("value")).AsInt() >= 0 ? TEXT("+") : TEXT(""), X.Get(TEXT("value")).AsInt()));
		Parts.Add(FString::Printf(TEXT("Profit die: +%d"), ProfitDie));
		Doc.Bullets(Parts);
		Doc.P(FString::Printf(TEXT("<big>Earned %dƒ.</>%s"), E.Get(TEXT("profit")).AsInt(), bOffshore ? *FString::Printf(TEXT(" <small>(Offshore wait: %dƒ paid.)</>"), E.Get(TEXT("fee")).AsInt()) : TEXT("")));
		if (bInfected && bOffshore) Doc.P(TEXT("<risk>Infected cargo! Profit halved. The ship waited offshore, so the sickness showed before anyone landed: no reputation lost and the plague does not spread.</>"));
		else if (bInfected)
		{
			const FString Outcome = Spread == TEXT("early") ? FString::Printf(TEXT("The plague reaches %s earlier than it really did."), *Esc(To)) : Spread == TEXT("worse") ? FString::Printf(TEXT("The plague in %s grows worse."), *Esc(To)) : FString(TEXT("The infection dies out this time."));
			Doc.P(FString::Printf(TEXT("<risk>Infected cargo! Profit halved and −%d reputation. %s</>"), Cfg(TEXT("penalties.infectedCargoReputation")), *Outcome));
		}
		if (E.Get(TEXT("partner")).IsNumber()) Doc.P(FString::Printf(TEXT("Partner %s also earns %dƒ."), *Esc(State.players[E.Get(TEXT("partner")).AsInt()].name), Cfg(TEXT("gains.dealBonus"))));
		if (bInfected || bOffshore) Doc.Note(Ids(E.Get(TEXT("factIds"))));
		// A clean, ordinary shipment closes by itself once the dice have landed.
		Opts.bDismissable = true;
		Opts.AutoClose = bInfected || bOffshore || Spread == TEXT("early") ? 0.f : Cfg(TEXT("timing.shipResultAutoCloseMs")) / 1000.f;
	}
	else if (Kind == TEXT("spread"))
	{
		const V& Arrival = D.Get(TEXT("arrival"));
		Doc.H2(TEXT("The plague spreads by trade"));
		Doc.P(Esc(Arrival.Get(TEXT("text")).AsString()));
		FPortsDoc Tray;
		Tray.AddBuilt(ArrivalRow(State, Arrival));
		Doc.Tray(Tray);
		Doc.Note(Ids(Arrival.Get(TEXT("factIds"))));
		Opts.bDismissable = true;
	}
	else if (Kind == TEXT("physician"))
	{
		const V& E = D.Get(TEXT("e"));
		const V* Remedy = Data().Remedies().GetItems().FindByPredicate([&E](const V& R) { return R.Get(TEXT("id")) == E.Get(TEXT("remedy")); });
		FPortsDoc Body;
		if (Remedy) Body.P(Esc(Remedy->Get(TEXT("text")).AsString()));
		Body.P(FString::Printf(TEXT("<b>It will not cure the plague.</> Only nursing care might help a little: if a family member in %s would die this round, they get one more roll and survive on a %d."), *Esc(CityName(E.Get(TEXT("city")).AsString())), Cfg(TEXT("plague.physicianSaveOn"))));
		Body.Note(Ids(E.Get(TEXT("factIds"))));
		Doc.Card(TEXT("medical"), TEXT("Remedy of the time"), Remedy ? Remedy->Get(TEXT("name")).AsString() : FString(), Body);
		Opts.bDismissable = true;
	}
	else if (Kind == TEXT("wage"))
	{
		const V& Entry = D.Get(TEXT("entry"));
		Doc.H2(TEXT("Wage inspection"));
		FPortsDoc Tray;
		Tray.Row({ PortsUi::Die(Entry.Get(TEXT("die")).AsInt(), false, false, false, TEXT("Inspection die")) }, 8, HAlign_Center);
		Doc.Tray(Tray);
		Doc.P(Esc(Entry.Get(TEXT("text")).AsString()));
		Opts.bDismissable = true;
	}
	else if (Kind == TEXT("reveal"))
	{
		const V& Card = Ports::CardById(D.Get(TEXT("cardId")).AsString());
		Doc.H2(Esc(Card.Get(TEXT("title")).AsString()));
		Doc.P(Esc(D.Get(TEXT("entry")).Get(TEXT("text")).AsString()));
		Doc.Note(Ids(Card.Get(TEXT("factIds"))));
		Opts.bDismissable = true;
	}
}

void UPortsGameFlow::OpenStoryPage(const FString& Kind, const V& Data)
{
	if (!TestHold.IsEmpty() && Kind == TestHold) { bAutoPlay = false; TestHold.Reset(); }
	FPortsDialogOptions Opts;
	FString Button;
	const TSharedRef<FPortsDoc> Doc = MakeShared<FPortsDoc>();
	BuildStory(Kind, Data, *Doc, Button, Opts);
	Doc->Width(Opts.InnerWidth());
	// A bot's cards go on by themselves after a few seconds (anyone can press the button sooner).
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	const bool bBotCard = P && P->bot && Opts.AutoClose <= 0.f;
	if (bBotCard) Opts.AutoClose = Cfg(TEXT("bots.cardSeconds"));
	const FString Label = bBotCard ? FString::Printf(TEXT("%s <lsmall>(or wait %d seconds)</>"), *PortsUi::Esc(Button), Cfg(TEXT("bots.cardSeconds"))) : PortsUi::Esc(Button);
	Open([Doc, Label](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(Label + TEXT("  <lsmall>Enter</>"), [Close]() { Close(TEXT("ok")); }, PortsUi::EButton::Primary) });
		return Doc->Widget();
	}, Opts, nullptr);
}

// A round's Chronicle cards are dealt over pages of at most four (chroniclePages in stories.js).
void UPortsGameFlow::Story(const FString& Kind, const V& Data, TFunction<void()> After)
{
	TArray<V> Pages;
	if (Kind == TEXT("chronicle"))
	{
		const V& Groups = Data.Get(TEXT("groups"));
		const int32 Count = FMath::Max(1, FMath::CeilToInt32(Groups.Num() / 4.0));
		const int32 Size = FMath::CeilToInt32(static_cast<double>(Groups.Num()) / Count);
		for (int32 i = 0; i < Count; i++)
		{
			V Slice = V::Array();
			for (int32 k = i * Size; k < FMath::Min(Groups.Num(), (i + 1) * Size); k++) Slice.Add(Groups[k]);
			Pages.Add(V::Object({ { TEXT("groups"), Slice }, { TEXT("page"), i + 1 }, { TEXT("pages"), Count } }));
		}
	}
	else Pages.Add(Data);
	for (const V& Page : Pages) Steps.Add([this, Kind, Page]() { OpenStoryPage(Kind, Page); });
	if (After) Steps.Add(After);
}
