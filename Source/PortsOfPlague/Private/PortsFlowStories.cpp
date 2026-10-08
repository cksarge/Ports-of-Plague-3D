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

	// plagueRound in stories.js: what every page of a round's plague results needs to know about the whole round.
	V PlagueRound(const V& Group)
	{
		int32 Halves = 0;
		bool bPre = true, bRolls = false, bDeaths = false;
		for (const V& E : Group.GetItems())
		{
			const FString Type = E.Get(TEXT("type")).AsString();
			if (Type == TEXT("plague")) { Halves++; if (!E.Get(TEXT("pre")).Truthy()) bPre = false; }
			if (Type == TEXT("mortality")) bRolls = true;
			if (E.Get(TEXT("deaths")).AsInt() > 0) bDeaths = true;
		}
		return V::Object({ { TEXT("halves"), Halves }, { TEXT("pre"), bPre }, { TEXT("rolls"), bRolls }, { TEXT("deaths"), bDeaths } });
	}

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
	// The big screen of a multi-device game: wider cards, shrunk to fit if they must be (body.big-screen in game.css).
	Opts.bBig = Remote();
	Opts.bFit = Remote();

	if (Kind == TEXT("lesson")) { BuildLesson(D.Get(TEXT("id")).AsString(), Doc, Button); return; }
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
		// On a big screen the story and the "how to play" box can be two cards (part: "story", then "how").
		const FString Part = D.Get(TEXT("part")).AsString();
		FPortsDoc Body;
		Body.P(Esc(Text)).Note(Ids(E.Get(TEXT("factIds"))));
		if (Part == TEXT("how")) Doc.H2(TEXT("Before you begin"));
		else Doc.Card(TEXT("trade"), TEXT("Prologue · 1346"), TEXT("The Siege of Caffa"), Body);
		if (Ui.hints && Part != TEXT("story"))
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
		Button = Part == TEXT("story") ? TEXT("How to play") : TEXT("Roll for turn order");
	}
	else if (Kind == TEXT("order"))
	{
		// Each house rolls a die; the highest goes first (ties roll again).
		const V& E = D.Get(TEXT("e"));
		Doc.H2(TEXT("Rolling for Turn Order"), ETextJustify::Center);
		Doc.P(TEXT("The highest roll goes first; tied houses roll again. <b>This order stays the same for the whole game.</>"), ETextJustify::Center);
		// On a big screen the rolls and the order they give can be two cards (part: "rolls", then "result").
		const FString Part = D.Get(TEXT("part")).AsString();
		const V& Rounds = E.Get(TEXT("rolls"));
		for (int32 R = 0; R < Rounds.Num() && Part != TEXT("result"); R++)
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
			// A tie's second throw is not shown, nor even announced, until the throw before it has come to rest.
			FPortsDoc Whole;
			Whole.Tray(Tray);
			const float ShowAt = R == 0 ? 0.f : PortsUi::DiceTrayStart() - 0.1f;
			Doc.AddBuilt([Whole, ShowAt](float W) { return ShowAt > 0.f ? PortsUi::Entrance(Whole.Build(W), 0, ShowAt) : Whole.Build(W); }, FMargin(0));
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
		if (Part.IsEmpty()) Doc.AddBuilt([Result, After](float W) { return PortsUi::Entrance(Result.Build(W), 0, After); });
		else if (Part == TEXT("result")) Doc.Nest(Result);
		Button = Part == TEXT("rolls") ? FString(TEXT("See the turn order")) : State.preRounds ? FString::Printf(TEXT("Begin the year %s"), *YearOf(Ports::HalfInfo(State.firstHalf).Get(TEXT("label")).AsString())) : FString(TEXT("Begin the year 1347"));
		Opts.bWide = true;
	}
	else if (Kind == TEXT("round"))
	{
		const V& Group = D.Get(TEXT("group"));
		const V& Head = Group[0];
		const FPortsRoundInfo Info = Ports::RoundInfo(State);
		const V& Half = Ports::HalfInfo(State.round);
		const FString Years = State.round == State.roundEnd ? Half.Get(TEXT("label")).AsString() : Info.label;
		// A long list of struck cities is dealt over several cards on a big screen (page, pages, facts).
		const int32 Page = D.Get(TEXT("page")).AsInt(1), Pages = D.Get(TEXT("pages")).AsInt(1);
		Opts.bWide = Group.Num() > 5;
		if (Page > 1) Doc.H2(FString::Printf(TEXT("%s: the plague arrives%s"), *Esc(Years), *PageOf(Page, Pages)));
		else
		{
			FPortsDoc Banner;
			Banner.Text(FString::Printf(TEXT("%sROUND %d OF %d"), Info.pre ? TEXT("BEFORE THE PLAGUE · ") : TEXT(""), Ports::RoundNumber(State), Ports::TotalRounds(State)), TEXT("Ports.Small"), ETextJustify::Center, FMargin(0));
			Banner.Text(Esc(Years), TEXT("Ports.Year"), ETextJustify::Center, FMargin(0));
			Banner.Text(FString::Printf(TEXT("<i>%s</>  %s"), *Esc(Info.months), Half.Get(TEXT("season")).AsString() == TEXT("warm") ? TEXT("☀") : TEXT("❄")), TEXT("Ports.Body"), ETextJustify::Center, FMargin(0));
			Banner.P(Esc(Info.headline), ETextJustify::Center);
			Doc.Nest(Banner);
		}
		TArray<FString> Facts = Ids(Head.Get(TEXT("factIds")));
		if (Info.pre)
		{
			Doc.P(FString::Printf(TEXT("<b>Before the plague.</> Only Caffa and Tana on the Black Sea are Stricken. No Event card and no survival rolls this round, and trading posts cost %dƒ less: set up your trade while the ports are safe."), Cfg(TEXT("prePlague.postDiscount"))));
		}
		else if (Group.Num() > 1)
		{
			if (Page == 1)
			{
				Doc.H3(FString::Printf(TEXT("The plague arrives%s"), *PageOf(Page, Pages)));
				const int32 DiffMod = Cfg(*(Ports::DifficultyPath(State) + TEXT(".severityMod")));
				const FString DiffText = DiffMod ? FString::Printf(TEXT(" On %s difficulty every roll counts %s."), *Data().Config().Get(TEXT("difficulty")).Get(State.difficulty).Get(TEXT("label")).AsString(), DiffMod > 0 ? TEXT("1 higher") : TEXT("1 lower")) : FString();
				Doc.Small(FString::Printf(TEXT("Each newly struck city rolls the red <sb>severity die</> to see how badly the plague hits it: %s. Hard-hit Tuscany and Catalonia add 1; Flanders subtracts 1.%s"), *SeverityBands(), *DiffText));
			}
			// On a big screen the cities stand two to a row, so the card needs less room (.big-screen .choice-list).
			const int32 PerRow = Opts.bBig && Opts.InnerWidth() - 28.f >= 2 * 430.f + 10.f ? 2 : 1;
			FPortsDoc Tray;
			for (int32 i = 1; i < Group.Num(); i += PerRow)
			{
				TArray<FPortsDoc> Line;
				for (int32 k = i; k < FMath::Min(Group.Num(), i + PerRow); k++)
				{
					FPortsDoc One;
					One.AddBuilt(ArrivalRow(State, Group[k]), FMargin(0));
					Line.Add(One);
					Facts.Append(Ids(Group[k].Get(TEXT("factIds"))));
				}
				if (PerRow == 1) Tray.AddBuilt(ArrivalRow(State, Group[i]), FMargin(0, 3));
				else Tray.Columns(Line, 10, FMargin(0, 3));
			}
			Doc.Tray(Tray);
		}
		else if (Page == 1)
		{
			Doc.P(TEXT("No new cities are struck this time."));
		}
		Doc.Note(D.Has(TEXT("facts")) ? Ids(D.Get(TEXT("facts"))) : Facts);
		if (Page < Pages) Button = TEXT("More cities");
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
		// Two or four cards stand two to a row and three stand in one row; more than that (a big screen's page can
		// hold more) fill rows of up to three, so the longest words of their titles still fit.
		Opts.bWide = true;
		const int32 PerRow = Cards.Num() == 4 ? 2 : Cards.Num() <= 3 ? FMath::Max(1, Cards.Num()) : FMath::Clamp(FMath::FloorToInt32((Opts.InnerWidth() + 13.f) / 273.f), 1, 3);
		for (int32 First = 0; First < Cards.Num(); First += PerRow)
		{
			TArray<FPortsDoc> Line;
			for (int32 k = First; k < FMath::Min(Cards.Num(), First + PerRow); k++) Line.Add(Cards[k]);
			Doc.Columns(Line, 14, FMargin(0, 3));
		}
		if (Facts.Num() > 4) Facts.SetNum(4);
		Doc.Note(D.Has(TEXT("facts")) ? Ids(D.Get(TEXT("facts"))) : Facts);
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
		// What every page of a round's plague results needs to know about the whole round (plagueRound in stories.js).
		const V Whole = D.Get(TEXT("whole")).IsObject() ? D.Get(TEXT("whole")) : PlagueRound(Group);
		const int32 Halves = Whole.Get(TEXT("halves")).AsInt();
		const bool bPre = Whole.Get(TEXT("pre")).Truthy(), bRolls = Whole.Get(TEXT("rolls")).Truthy(), bDeaths = Whole.Get(TEXT("deaths")).Truthy();
		const int32 Page = D.Get(TEXT("page")).AsInt(1), Pages = D.Get(TEXT("pages")).AsInt(1);
		const bool bLast = Page == Pages;
		Opts.bWide = true;
		const FString Label = Ports::RoundInfo(State).label;
		if (bPre)
		{
			Doc.H2(FString::Printf(TEXT("The Year Turns: %s"), *Esc(Label)));
			Doc.P(TEXT("No plague yet: only upkeep is paid."));
		}
		else
		{
			Doc.H2(FString::Printf(TEXT("The Plague Takes Its Toll: %s%s"), *Esc(Label), *PageOf(Page, Pages)));
			if (Page == 1) Doc.P(TEXT("Every family member in a Stricken city rolls the mortality die."));
			const int32 Bonus = Cfg(TEXT("plague.prepareBonus"));
			// Runs of dice trays and of Aftermath lines are grouped, so a big screen can show them side by side
			// (.big-screen .plague-grid and .aftermath-list); elsewhere they stand one under another.
			const float Inner = Opts.InnerWidth();
			const int32 TrayColumns = Opts.bBig ? FMath::Max(1, FMath::FloorToInt32((Inner + 13.f) / (380.f + 13.f))) : 1;
			const int32 AfterColumns = Opts.bBig ? FMath::Clamp(FMath::FloorToInt32((Inner + 24.f) / (300.f + 24.f)), 1, 3) : 1;
			TArray<FPortsDoc> Trays;
			TArray<FString> After;
			const auto Flush = [&Doc, &Trays, &After, TrayColumns, AfterColumns]()
			{
				for (int32 First = 0; First < Trays.Num(); First += TrayColumns)
				{
					TArray<FPortsDoc> Line;
					for (int32 k = First; k < FMath::Min(Trays.Num(), First + TrayColumns); k++) Line.Add(Trays[k]);
					if (TrayColumns == 1) Doc.Nest(Line[0], FMargin(0));
					else Doc.Columns(Line, 13, FMargin(0));
				}
				if (AfterColumns == 1 || After.Num() < 2) for (const FString& Line : After) Doc.P(Line);
				else
				{
					const int32 N = FMath::Min(AfterColumns, After.Num());
					const int32 Each = FMath::DivideAndRoundUp(After.Num(), N);
					TArray<FPortsDoc> Columns;
					for (int32 c = 0; c < N; c++)
					{
						FPortsDoc Column;
						for (int32 k = c * Each; k < FMath::Min(After.Num(), (c + 1) * Each); k++) Column.P(After[k]);
						Columns.Add(Column);
					}
					Doc.Columns(Columns, 24, FMargin(0));
				}
				Trays.Reset();
				After.Reset();
			};
			for (const V& E : Group.GetItems())
			{
				const FString Type = E.Get(TEXT("type")).AsString();
				if (Type != TEXT("mortality") && Type != TEXT("aftermath")) Flush();
				if (Type == TEXT("plague"))
				{
					if (Halves > 1) Doc.H3(Esc(Ports::HalfInfo(E.Get(TEXT("half")).AsInt()).Get(TEXT("label")).AsString()));
					if (E.Get(TEXT("pre")).Truthy()) Doc.P(Esc(E.Get(TEXT("text")).AsString()));
				}
				else if (Type == TEXT("aftermath")) After.Add(FString::Printf(TEXT("❦ %s"), *Esc(E.Get(TEXT("text")).AsString())));
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
					FPortsDoc One;
					One.Tray(Tray);
					Trays.Add(One);
				}
			}
			Flush();
			if (!bRolls && bLast) Doc.P(TEXT("No family members were in Stricken cities this round."));
			if (D.Has(TEXT("facts"))) Doc.Note(Ids(D.Get(TEXT("facts"))));
			else if (bDeaths) Doc.Note({ TEXT("EC-10"), TEXT("DB-01") });
		}
		Button = !bLast ? TEXT("Continue") : State.roundEnd >= Cfg(TEXT("rounds")) ? TEXT("Final scoring") : TEXT("Begin the next round");
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
	const bool bBotCard = P && P->bot && Opts.AutoClose <= 0.f && Kind != TEXT("lesson");
	if (bBotCard) Opts.AutoClose = Cfg(TEXT("bots.cardSeconds"));
	const FString Label = bBotCard ? FString::Printf(TEXT("%s <lsmall>(or wait %d seconds)</>"), *PortsUi::Esc(Button), Cfg(TEXT("bots.cardSeconds"))) : PortsUi::Esc(Button);
	if (Remote())
	{
		// Every device can read this card and press Next on it (the titles are those of stories.js).
		const V& CardOf = Data.Get(TEXT("e")).IsObject() ? Data.Get(TEXT("e")) : Data.Get(TEXT("group")).IsArray() && Data.Get(TEXT("group")).Num() ? Data.Get(TEXT("group"))[0] : Data;
		const FString CardId = Kind == TEXT("reveal") ? Data.Get(TEXT("cardId")).AsString() : CardOf.Get(TEXT("card")).AsString();
		const int32 PageNo = Data.Get(TEXT("page")).AsInt(), PageCount = Data.Get(TEXT("pages")).AsInt();
		StoryTitle = Kind == TEXT("lesson") ? LessonTitle(Data.Get(TEXT("id")).AsString()) : Kind == TEXT("prologue") ? (Data.Get(TEXT("part")).AsString() == TEXT("how") ? TEXT("How to play") : TEXT("Prologue"))
			: Kind == TEXT("order") ? TEXT("Turn order")
			: Kind == TEXT("round") ? Ports::RoundInfo(State).label + (PageCount > 1 ? FString::Printf(TEXT(" (%d of %d)"), PageNo, PageCount) : FString())
			: Kind == TEXT("card") || Kind == TEXT("reveal") ? Ports::CardById(CardId).Get(TEXT("title")).AsString()
			: Kind == TEXT("chronicle") ? (PageCount > 1 ? FString::Printf(TEXT("Chronicle %d of %d"), PageNo, PageCount) : FString(TEXT("Chronicle")))
			: Kind == TEXT("fortune") ? TEXT("Fortune card: ") + Ports::FortuneById(CardId).Get(TEXT("title")).AsString()
			: Kind == TEXT("plague") ? FString((Data.Get(TEXT("whole")).IsObject() ? Data.Get(TEXT("whole")) : PlagueRound(Data.Get(TEXT("group")))).Get(TEXT("pre")).Truthy() ? TEXT("End of the round") : TEXT("Plague results")) + (PageCount > 1 ? FString::Printf(TEXT(" (%d of %d)"), PageNo, PageCount) : FString())
			: Kind == TEXT("ship") ? TEXT("Shipment result") : Kind == TEXT("spread") ? TEXT("Plague spreads") : Kind == TEXT("physician") ? TEXT("Physician") : Kind == TEXT("wage") ? TEXT("Wage inspection") : TEXT("Continue");
		StoryId = ++StorySeq;
		StoryLabel = Button;
		// A tutorial lesson is not a card the web version knows: its device shows the title and the button only.
		StoryKind = Kind == TEXT("lesson") ? FString() : Kind;
		StoryData = Kind == TEXT("lesson") ? V::Null() : Data;
		PushToDevices();
	}
	Open([Doc, Label](TFunction<void(const FString&)> Close)
	{
		Doc->Buttons({ PortsUi::Button(Label + TEXT("  <lsmall>Enter</>"), [Close]() { Close(TEXT("ok")); }, PortsUi::EButton::Primary) });
		return Doc->Widget();
	}, Opts, [this](const FString&) { StoryId = 0; PushToDevices(); });
}

// How far a big screen would have to shrink this card to show all of it (zoomNeeded in game.js).
float UPortsGameFlow::StoryZoom(const FString& Kind, const V& Page) const
{
	FPortsDialogOptions Opts;
	FString Button;
	FPortsDoc Doc;
	BuildStory(Kind, Page, Doc, Button, Opts);
	Doc.Buttons({ PortsUi::Button(PortsUi::Esc(Button) + TEXT("  <lsmall>Enter</>"), []() {}, PortsUi::EButton::Primary) });
	const TSharedRef<SWidget> Built = Doc.Build(Opts.InnerWidth());
	Built->SlatePrepass(Root->LayoutScale());
	const float Space = Root->ViewHeight() * 0.9f - 64.f;
	const float Tall = Built->GetDesiredSize().Y;
	return Tall <= Space ? 1.f : FMath::Max(0.45f, Space / FMath::Max(1.f, Tall));
}

// The cards one story is dealt out over. A round's Chronicle cards go over pages of at most four
// (chroniclePages in stories.js). On the big screen of a multi-device game nobody can scroll, so there a long
// card is dealt over as many cards as keep it easy to read: no card shrinks below 0.8 of its size while it
// holds more than one piece (splitStory in stories.js, storyPages in game.js).
TArray<V> UPortsGameFlow::StoryPages(const FString& Kind, const V& Data) const
{
	TArray<V> Fallback;
	if (Kind == TEXT("chronicle"))
	{
		const V& Groups = Data.Get(TEXT("groups"));
		const int32 Count = FMath::Max(1, FMath::CeilToInt32(Groups.Num() / 4.0));
		const int32 Size = FMath::CeilToInt32(static_cast<double>(Groups.Num()) / Count);
		for (int32 i = 0; i < Count; i++)
		{
			V Slice = V::Array();
			for (int32 k = i * Size; k < FMath::Min(Groups.Num(), (i + 1) * Size); k++) Slice.Add(Groups[k]);
			Fallback.Add(V::Object({ { TEXT("groups"), Slice }, { TEXT("page"), i + 1 }, { TEXT("pages"), Count } }));
		}
	}
	else Fallback.Add(Data);
	if (!Remote()) return Fallback;

	using FParts = TArray<TArray<V>>;
	TArray<V> Units;
	TFunction<TArray<V>(const FParts&)> Make;
	const auto Numbered = [](TArray<V> Pages) { for (int32 i = 0; i < Pages.Num(); i++) { Pages[i].Set(TEXT("page"), i + 1); Pages[i].Set(TEXT("pages"), Pages.Num()); } return Pages; };
	// Each historical fact is a piece of its own, named once.
	const auto AddFacts = [&Units, this](const TArray<FString>& FactIds)
	{
		if (!Ui.history) return;
		TArray<FString> Seen;
		for (const FString& Id : FactIds)
		{
			if (Seen.Contains(Id) || !FPortsData::Get().Facts().GetItems().ContainsByPredicate([&Id](const V& F) { return F.Get(TEXT("id")).AsString() == Id; })) continue;
			Seen.Add(Id);
			Units.Add(V::Object({ { TEXT("fact"), V(Id) } }));
		}
	};
	const auto FactsIn = [](const TArray<V>& Part) { V Out = V::Array(); for (const V& U : Part) if (U.Has(TEXT("fact"))) Out.Add(U.Get(TEXT("fact"))); return Out; };

	if ((Kind == TEXT("prologue") && Ui.hints) || Kind == TEXT("order"))
	{
		const bool bOrder = Kind == TEXT("order");
		Units = { V(bOrder ? TEXT("rolls") : TEXT("story")), V(bOrder ? TEXT("result") : TEXT("how")) };
		const TArray<V> Names = Units;
		Make = [Data, Names](const FParts& Parts)
		{
			TArray<V> Out;
			if (Parts.Num() < 2) { Out.Add(Data); return Out; }
			for (const V& Name : Names) { V Page = Data; Page.Set(TEXT("part"), Name); Out.Add(Page); }
			return Out;
		};
	}
	else if (Kind == TEXT("round"))
	{
		const V& Group = Data.Get(TEXT("group"));
		const V Head = Group[0];
		TArray<FString> FactIds = Head.Get(TEXT("factIds")).ToStrings();
		for (int32 i = 1; i < Group.Num(); i++) { Units.Add(V::Object({ { TEXT("arrival"), Group[i] } })); FactIds.Append(Group[i].Get(TEXT("factIds")).ToStrings()); }
		AddFacts(FactIds);
		Make = [Head, Numbered, FactsIn](const FParts& Parts)
		{
			TArray<V> Out;
			for (const TArray<V>& Part : Parts)
			{
				V PageGroup = V::Array({ Head });
				for (const V& U : Part) if (U.Has(TEXT("arrival"))) PageGroup.Add(U.Get(TEXT("arrival")));
				Out.Add(V::Object({ { TEXT("group"), PageGroup }, { TEXT("facts"), FactsIn(Part) } }));
			}
			return Numbered(Out);
		};
	}
	else if (Kind == TEXT("chronicle"))
	{
		TArray<FString> FactIds;
		for (const V& G : Data.Get(TEXT("groups")).GetItems()) Units.Add(V::Object({ { TEXT("group"), G } }));
		// The facts each page would show where cards can scroll: up to four a page.
		for (const V& Page : Fallback)
		{
			TArray<FString> OfPage;
			for (const V& G : Page.Get(TEXT("groups")).GetItems()) OfPage.Append(Ports::CardById(G[0].Get(TEXT("card")).AsString()).Get(TEXT("factIds")).ToStrings());
			if (OfPage.Num() > 4) OfPage.SetNum(4);
			FactIds.Append(OfPage);
		}
		AddFacts(FactIds);
		Make = [Numbered, FactsIn](const FParts& Parts)
		{
			TArray<V> Out;
			for (const TArray<V>& Part : Parts)
			{
				V Groups = V::Array();
				for (const V& U : Part) if (U.Has(TEXT("group"))) Groups.Add(U.Get(TEXT("group")));
				Out.Add(V::Object({ { TEXT("groups"), Groups }, { TEXT("facts"), FactsIn(Part) } }));
			}
			return Numbered(Out);
		};
	}
	else if (Kind == TEXT("plague"))
	{
		const V& Group = Data.Get(TEXT("group"));
		const V Whole = PlagueRound(Group);
		if (Whole.Get(TEXT("pre")).Truthy()) return Fallback;
		// A half-year heading stays with the line that follows it.
		V Heads = V::Array();
		for (const V& E : Group.GetItems())
		{
			if (E.Get(TEXT("type")).AsString() == TEXT("plague")) { Heads.Add(E); continue; }
			V Lines = Heads;
			Lines.Add(E);
			Units.Add(V::Object({ { TEXT("lines"), Lines } }));
			Heads = V::Array();
		}
		if (Heads.Num())
		{
			if (Units.Num()) { V Lines = Units.Last().Get(TEXT("lines")); for (const V& H : Heads.GetItems()) Lines.Add(H); Units.Last().Set(TEXT("lines"), Lines); }
			else Units.Add(V::Object({ { TEXT("lines"), Heads } }));
		}
		if (Whole.Get(TEXT("deaths")).Truthy()) AddFacts({ TEXT("EC-10"), TEXT("DB-01") });
		Make = [Whole, Numbered, FactsIn](const FParts& Parts)
		{
			TArray<V> Out;
			for (const TArray<V>& Part : Parts)
			{
				V PageGroup = V::Array();
				for (const V& U : Part) for (const V& Line : U.Get(TEXT("lines")).GetItems()) PageGroup.Add(Line);
				Out.Add(V::Object({ { TEXT("group"), PageGroup }, { TEXT("facts"), FactsIn(Part) }, { TEXT("whole"), Whole } }));
			}
			return Numbered(Out);
		};
	}
	if (!Make || Units.Num() == 0) return Fallback;

	constexpr float Readable = 0.8f;
	const auto Fits = [this, &Kind](const V& Page) { return StoryZoom(Kind, Page) >= Readable; };
	FParts Parts;
	Parts.AddDefaulted();
	for (const V& Unit : Units)
	{
		FParts Fuller = Parts;
		Fuller.Last().Add(Unit);
		if (Parts.Last().Num() && !Fits(Make(Fuller).Last())) Parts.Add({ Unit });
		else Parts = Fuller;
	}
	// The same number of cards, evenly filled, when that is readable too (share in stories.js).
	FParts Even;
	for (int32 i = 0; i < Parts.Num(); i++)
	{
		const int32 From = FMath::RoundToInt32(static_cast<double>(i) * Units.Num() / Parts.Num()), To = FMath::RoundToInt32(static_cast<double>(i + 1) * Units.Num() / Parts.Num());
		if (To > From) Even.Add(TArray<V>(Units.GetData() + From, To - From));
	}
	const TArray<V> EvenPages = Make(Even);
	bool bEvenFits = true;
	for (const V& Page : EvenPages) bEvenFits = bEvenFits && Fits(Page);
	const TArray<V> Dealt = bEvenFits ? EvenPages : Make(Parts);
	FString Sizes;
	for (const V& Page : Dealt) Sizes += FString::Printf(TEXT(" %.2f"), StoryZoom(Kind, Page));
	UE_LOG(LogTemp, Display, TEXT("PortsStory: %s in %d pieces over %d cards (%s), each shown at%s of its size."), *Kind, Units.Num(), Dealt.Num(), bEvenFits ? TEXT("evenly") : TEXT("as they fit"), *Sizes);
	return Dealt;
}

void UPortsGameFlow::Story(const FString& Kind, const V& Data, TFunction<void()> After)
{
	// The pages are settled when the story's turn comes, not before: by then the screen and the game are as they will be shown.
	Steps.Add([this, Kind, Data, After]()
	{
		const TArray<V> Pages = StoryPages(Kind, Data);
		TArray<TFunction<void()>> Mine;
		for (const V& Page : Pages) Mine.Add([this, Kind, Page]() { OpenStoryPage(Kind, Page); });
		if (After) Mine.Add(After);
		Steps.Insert(Mine, 0);
	});
}
