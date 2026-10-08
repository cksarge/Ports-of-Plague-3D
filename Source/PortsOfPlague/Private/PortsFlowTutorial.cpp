// The tutorial (not on the website): a real, short game against one computer
// house, with a lesson card the first time each part of the game comes up.
// The lessons only explain the rules as rulebook.json states them, with the
// numbers read from config.json; they change nothing about how the game plays.
//
// On a player's own device a lesson is read on the big screen: the device is
// sent only the card's title and its button (no kind, no data), which the web
// version's join page already knows how to show.
#include "PortsGameFlow.h"

#include "PortsUi.h"
#include "SPortsRoot.h"

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

	struct FLesson
	{
		FString Title;
		TArray<FString> Paragraphs;
		TArray<FString> Bullets;
		// Said after the bullets.
		TArray<FString> After;
		FString Button = TEXT("Continue");
	};
}

// The first time a lesson is due it is queued like any other card; after that it never comes again in this game.
bool UPortsGameFlow::Lesson(const FString& Id)
{
	if (!Ui.tutorial || Ui.lessons.Contains(Id)) return false;
	Ui.lessons.Add(Id);
	Save();
	UE_LOG(LogTemp, Display, TEXT("PortsLesson: %s (round %d)"), *Id, Ports::RoundNumber(State));
	Sound(TEXT("page"));
	Story(TEXT("lesson"), V::Object({ { TEXT("id"), V(Id) } }));
	return true;
}

// Lessons about what is about to be shown: asked before each new card of the game.
bool UPortsGameFlow::TutorialBefore()
{
	if (!Ui.tutorial) return false;
	const int32 Index = State.log.IndexOfByPredicate([this](const V& E) { return E.Get(TEXT("seq")).AsInt() > Ui.seenSeq; });
	if (Index == INDEX_NONE) return false;
	const V& Next = State.log[Index];
	const FString Type = Next.Get(TEXT("type")).AsString();
	if (Type == TEXT("prologue")) return Lesson(TEXT("welcome"));
	if (Type == TEXT("round"))
	{
		if (Lesson(TEXT("round"))) return true;
		const bool bArrival = State.log.IsValidIndex(Index + 1) && State.log[Index + 1].Get(TEXT("type")).AsString() == TEXT("arrival");
		return bArrival && Lesson(TEXT("arrival"));
	}
	if (Type == TEXT("card")) return Lesson(Next.Get(TEXT("deck")).AsString() == TEXT("chronicle") ? TEXT("chronicle") : TEXT("event"));
	if (Type == TEXT("fortune"))
	{
		const int32 Id = Next.Get(TEXT("player")).AsInt();
		return State.players.IsValidIndex(Id) && !State.players[Id].bot && Lesson(TEXT("fortune"));
	}
	if (Type == TEXT("plague")) return !Next.Get(TEXT("pre")).Truthy() && Lesson(TEXT("survival"));
	return false;
}

// Lessons about the learner's own turn: asked whenever the turn is theirs and nothing else is on screen.
bool UPortsGameFlow::TutorialTurn(const FPortsPlayer& P)
{
	using namespace Ports;
	if (!Ui.tutorial || P.bot) return false;
	if (Lesson(TEXT("turn"))) return true;
	if (P.pending.Num()) return Lesson(TEXT("decision"));
	if (P.shipped.Num() && Lesson(TEXT("shipped"))) return true;
	for (const FString& L : FamilyLocations(P)) if (L != ESTATE && IsStricken(State, L) && Lesson(TEXT("danger"))) return true;
	const bool bFullTurn = P.ap >= Cfg(*(TEXT("modes.") + State.mode + TEXT(".actionPoints")));
	if (Ui.lessons.Contains(TEXT("shipped")) && P.posts.Num() < 2 && P.florins >= Cost(State, TEXT("openPost"), &P) && P.ap >= ApCost(TEXT("post")) && Lesson(TEXT("post"))) return true;
	if (P.posts.ContainsByPredicate([this](const FString& C) { return IsAftermath(State, C); }) && Lesson(TEXT("aftermath"))) return true;
	if (bFullTurn && RoundNumber(State) >= 2 && Lesson(TEXT("legacy"))) return true;
	if (bFullTurn && RoundNumber(State) >= 3 && Lesson(TEXT("more"))) return true;
	return false;
}

void UPortsGameFlow::BuildLesson(const FString& Id, FPortsDoc& Doc, FString& Button) const
{
	using namespace Ports;
	// Whose lesson it is: the one house a person plays.
	const FPortsPlayer* Me = State.players.FindByPredicate([](const FPortsPlayer& P) { return !P.bot; });
	const int32 Ap = Cfg(*(TEXT("modes.") + State.mode + TEXT(".actionPoints")));
	const int32 PostAp = Cfg(TEXT("actionPointCosts.post"));
	const FString Where = Remote() ? TEXT("on your own device") : TEXT("in the panel on the right");
	FLesson L;
	if (Id == TEXT("welcome"))
	{
		L.Title = TEXT("Welcome to the tutorial");
		L.Paragraphs = {
			TEXT("This is a real game, only a short one: Quick Play on Apprentice difficulty, you against one computer house. A card like this one appears the first time something new happens, to say what it means and what you can do."),
			TEXT("<b>Your aim</> is the highest <b>Legacy</> when the game ends in 1353. Legacy is Wealth + Family + Reputation, plus your weakest of the three again. A balanced house usually beats a greedy one."),
			Remote() ? TEXT("Lesson cards are read here on the big screen. Press the button here or on your own device to go on.") : TEXT("Next comes the Prologue, and then each house rolls a die for the turn order."),
		};
		L.Button = TEXT("Begin");
	}
	else if (Id == TEXT("round"))
	{
		L.Title = TEXT("How a round works");
		L.Paragraphs = { TEXT("Every round has four phases:") };
		L.Bullets = {
			TEXT("<b>Chronicle.</> The date advances, the plague reaches new cities, and the round's dated Chronicle cards are read."),
			TEXT("<b>Event.</> One Event card is drawn."),
			FString::Printf(TEXT("<b>Actions.</> The houses take their turns in the order just rolled, each with <b>%d action points</>."), Ap),
			TEXT("<b>Plague & upkeep.</> Family members in Stricken cities roll for survival."),
		};
		if (State.preRounds > 0) L.After.Add(FString::Printf(TEXT("This first round is a <b>pre-plague round</>: there is no Event card and no survival roll, and opening a trading post costs %dƒ less. Use it to set up your trade before the plague sails west."), Cfg(TEXT("prePlague.postDiscount"))));
		L.After.Add(TEXT("After it, each round of this Quick Play game covers a year and a half."));
	}
	else if (Id == TEXT("turn"))
	{
		L.Title = TEXT("Your turn");
		L.Paragraphs = {
			FString::Printf(TEXT("You have <b>%d action points</> (AP). Your actions are listed %s; each one also has a key. Most actions take 1 AP; Open Trading Post and Move Family take %d."), Ap, *Where, PostAp),
			FString::Printf(TEXT("Start with <key>1</> <b>Ship Goods</>: choose your trading post in %s, then a route leaving it. You earn the route's value plus the profit die. Sea routes are solid lines and land routes dashed lines; each shows its value."), Me ? *Esc(CityName(Me->home)) : TEXT("your home city")),
			TEXT("When you have done all you want, press <key>E</> <b>End Turn</>. Unused action points are lost."),
		};
		L.Button = TEXT("Take my turn");
	}
	else if (Id == TEXT("shipped"))
	{
		L.Title = TEXT("Your first shipment");
		L.Paragraphs = {
			FString::Printf(TEXT("A shipment earns the route's value + the profit die, + %d if a family member lives at that post. Each trading post ships <b>once a round</>, so more posts mean more shipments."), Cfg(TEXT("gains.familyAtPostBonus"))),
			FString::Printf(TEXT("A <b>%d</> on the profit die also draws a <b>Fortune card</>, a personal card that may help you or hurt you."), Cfg(TEXT("fortune.drawOnProfitDie"))),
			FString::Printf(TEXT("Later, shipping <b>from a Stricken city</> rolls the contagion die as well. If it is equal to or lower than that city's severity, the cargo is infected: you earn only half the profit and lose %d reputation, and the plague may spread to where you shipped."), Cfg(TEXT("penalties.infectedCargoReputation"))),
		};
	}
	else if (Id == TEXT("post"))
	{
		L.Title = TEXT("A second trading post");
		L.Paragraphs = {
			FString::Printf(TEXT("<key>2</> <b>Open Trading Post</> takes %d AP and, this round, %dƒ. The new post must be in a city joined by a route to one of your posts, and it cannot be a Stricken city."), PostAp, Me ? Cost(State, TEXT("openPost"), Me) : Cfg(TEXT("costs.openPost"))),
			FString::Printf(TEXT("A second post lets you ship from two places, opening it draws a Fortune card, and every post is worth %d Wealth point at the end. You may own at most %d."), Cfg(TEXT("scoring.pointsPerPost")), Cfg(TEXT("limits.maxPosts"))),
		};
	}
	else if (Id == TEXT("fortune"))
	{
		L.Title = TEXT("Fortune cards");
		L.Paragraphs = {
			FString::Printf(TEXT("You have drawn a Fortune card. You draw one whenever you roll a %d on the profit die or open a new trading post."), Cfg(TEXT("fortune.drawOnProfitDie"))),
			TEXT("Fortune cards are yours alone: spice cargoes, extra action points, free posts, warnings of where the plague goes next, but also demanding workers, illness and nervous creditors. If one makes you an offer, you accept or decline at once."),
		};
	}
	else if (Id == TEXT("arrival"))
	{
		L.Title = TEXT("The plague arrives");
		L.Paragraphs = {
			TEXT("At the start of each round the plague reaches the cities it really reached in those months. Each one becomes <b>Stricken</> (red ring) and rolls for severity: Light, Heavy or Devastating, shown as 1 to 3 pips. On Apprentice difficulty every roll is 1 lower."),
			TEXT("A Safe city next to a Stricken one is <b>Threatened</> (amber ring): the plague may arrive soon."),
			TEXT("You cannot open a trading post in a Stricken city. Click any city on the map to see how it stands."),
		};
	}
	else if (Id == TEXT("chronicle"))
	{
		L.Title = TEXT("Chronicle cards");
		L.Paragraphs = { TEXT("Chronicle cards are dated and always appear in their round. Some only tell what happened; others change the game for every house, and say so on the card.") };
	}
	else if (Id == TEXT("event"))
	{
		L.Title = TEXT("Event cards");
		L.Paragraphs = { TEXT("One Event card is drawn each round from a shuffled deck, so every game is different. Some affect every house; some fall on one house chosen at random. If the card offers a choice, each player decides at the start of their own turn.") };
	}
	else if (Id == TEXT("decision"))
	{
		L.Title = TEXT("A card needs your decision");
		L.Paragraphs = { FString::Printf(TEXT("An offer is waiting for you%s. Read what it costs and what it gives, then accept or decline; your turn goes on afterwards. Some offers are traps."), Remote() ? TEXT(" on your device") : TEXT("")) };
	}
	else if (Id == TEXT("danger"))
	{
		FString City;
		if (Me) for (const FString& Loc : FamilyLocations(*Me)) if (Loc != ESTATE && IsStricken(State, Loc)) { City = Loc; break; }
		L.Title = TEXT("Your family is in danger");
		L.Paragraphs = { FString::Printf(TEXT("%s is Stricken, and your family lives there. In the plague phase each family member in a Stricken city rolls the mortality die: equal to or lower than the city's severity, and that family member dies. You can:"), City.IsEmpty() ? TEXT("One of your cities") : *Esc(CityName(City))) };
		L.Bullets = {
			FString::Printf(TEXT("<key>3</> <b>Move Family</> (%d AP): move up to %d of them to another of your cities or to your Country Estate. Leaving a Stricken city is fleeing and costs %d reputation. Family at the Estate is safe from the plague, but earns no family bonus."), Cfg(TEXT("actionPointCosts.move")), Cfg(TEXT("limits.moveFamilyMax")), Cfg(TEXT("penalties.fleeReputation"))),
			FString::Printf(TEXT("<key>4</> <b>Prepare Household</> (1 AP, %dƒ): this round their survival rolls there get +%d."), Cfg(TEXT("costs.prepareHousehold")), Cfg(TEXT("plague.prepareBonus"))),
			FString::Printf(TEXT("<key>5</> <b>Consult Physician</> (1 AP, %dƒ): this round, the first family member there who would die rolls again and survives on a %d."), Cfg(TEXT("costs.physician")), Cfg(TEXT("plague.physicianSaveOn"))),
		};
		L.After = { TEXT("Or stay and trade. A house's last family member never dies, and no one is ever knocked out of the game.") };
	}
	else if (Id == TEXT("survival"))
	{
		L.Title = TEXT("Plague & upkeep");
		L.Paragraphs = {
			FString::Printf(TEXT("The round ends with the plague phase. Each family member in a Stricken city rolls the mortality die, adding +%d if the household was prepared. If the result is equal to or lower than the severity, that family member dies. For each one lost, the house inherits %dƒ."), Cfg(TEXT("plague.prepareBonus")), Cfg(TEXT("gains.inheritance"))),
			FString::Printf(TEXT("In Quick Play each round covers three half-years, so the plague phase happens three times. A city that has been Stricken for %d half-years moves to Aftermath."), Cfg(TEXT("plague.strickenRounds"))),
		};
	}
	else if (Id == TEXT("legacy"))
	{
		L.Title = TEXT("Keep all three healthy");
		L.Paragraphs = { TEXT("The number beside each house is its Legacy so far. It is made of:") };
		L.Bullets = {
			FString::Printf(TEXT("<b>Wealth:</> 1 point per %dƒ, plus %d per trading post and %d per land holding."), Cfg(TEXT("scoring.florinsPerPoint")), Cfg(TEXT("scoring.pointsPerPost")), Cfg(TEXT("scoring.pointsPerLand"))),
			FString::Printf(TEXT("<b>Family:</> %d per surviving family member."), Cfg(TEXT("scoring.pointsPerFamily"))),
			FString::Printf(TEXT("<b>Reputation:</> 1 per reputation point up to %d, then 1 per %d points above that."), Cfg(TEXT("scoring.reputationSoftCap")), Cfg(TEXT("scoring.reputationHighRate"))),
			TEXT("<b>Balance bonus:</> your lowest of the three, added again."),
		};
		L.After = { FString::Printf(TEXT("If reputation is your weakest, <key>6</> <b>Charity & Piety</> (1 AP, %dƒ) gives +%d reputation, once per turn."), Cfg(TEXT("costs.charity")), Cfg(TEXT("gains.charityReputation"))) };
	}
	else if (Id == TEXT("aftermath"))
	{
		FString City;
		if (Me) for (const FString& C : Me->posts) if (IsAftermath(State, C)) { City = C; break; }
		L.Title = TEXT("Aftermath");
		L.Paragraphs = { FString::Printf(TEXT("The plague has passed through %s, which is now in <b>Aftermath</> (grey ring): workers are scarce and prices are high. Shipping to an Aftermath city earns %d more; shipping from one costs a wage of %d. Two actions open up where you have a post in such a city:"), City.IsEmpty() ? TEXT("one of your cities") : *Esc(CityName(City)), Cfg(TEXT("gains.aftermathPriceBonus")), Cfg(TEXT("costs.wageAftermath"))) };
		L.Bullets = {
			FString::Printf(TEXT("<key>7</> <b>Arrange a Marriage</> (1 AP, %dƒ), where you also have family: +%d family member there. Your family cannot grow beyond %d. Once per turn."), Cfg(TEXT("costs.marriage")), Cfg(TEXT("gains.marriageFamily")), Cfg(TEXT("start.family"))),
			FString::Printf(TEXT("<key>8</> <b>Buy Abandoned Land</> (1 AP, %dƒ), one holding per city: worth %d Wealth points at the end, but you pay %dƒ per holding in every plague phase."), Cfg(TEXT("costs.buyLand")), Cfg(TEXT("scoring.pointsPerLand")), Cfg(TEXT("costs.landWage"))),
		};
	}
	else if (Id == TEXT("more"))
	{
		L.Title = TEXT("More you can do");
		L.Paragraphs = { TEXT("Three actions you have not needed yet:") };
		L.Bullets = {
			FString::Printf(TEXT("<key>9</> <b>Take a Loan</> (no AP): borrow %dƒ now and repay %dƒ in the plague phase of the next round. One loan at a time; not in the final round."), Cfg(TEXT("gains.loan")), Cfg(TEXT("costs.loanRepay"))),
			FString::Printf(TEXT("<key>0</> <b>Propose a Partnership</> (no AP): until the end of the next round, when either partner ships to a city where the other has a post, the shipper earns %dƒ more and the partner %dƒ."), Cfg(TEXT("gains.dealShipperBonus")), Cfg(TEXT("gains.dealBonus"))),
			FString::Printf(TEXT("<key>G</> <b>Close Your Gates</> (1 AP, −%d reputation): until the end of the next round, rival houses cannot open a post in that city and earn %dƒ less shipping to it."), Cfg(TEXT("penalties.gatesReputation")), Cfg(TEXT("penalties.gatesProfit"))),
		};
		L.After = { TEXT("Every rule is in <b>Rules</> (<key>R</>) whenever you want to check one.") };
	}
	else if (Id == TEXT("final"))
	{
		L.Title = TEXT("The end of the game");
		L.Paragraphs = {
			TEXT("It is 1353 and the game is over. Now each house's Legacy is counted: Wealth + Family + Reputation, plus its lowest of the three again. The highest Legacy wins; ties go to higher reputation, then more family."),
			FString::Printf(TEXT("That is the whole game. For a full one, choose <b>New game</>: a Standard game lasts %d rounds of half a year each, with up to %d houses, on one device or each on their own."), Cfg(TEXT("rounds")), Cfg(TEXT("players.max"))),
		};
		L.Button = TEXT("See the final scores");
	}
	Button = L.Button;
	Doc.Text(TEXT("<legend>Tutorial</>"), TEXT("Ports.Body"), ETextJustify::Left, FMargin(0));
	Doc.H2(Esc(L.Title));
	for (const FString& Paragraph : L.Paragraphs) Doc.P(Paragraph);
	if (L.Bullets.Num()) Doc.Bullets(L.Bullets);
	for (const FString& Paragraph : L.After) Doc.P(Paragraph);
}

FString UPortsGameFlow::LessonTitle(const FString& Id) const
{
	// The same titles the cards show.
	static const TMap<FString, FString> Titles = {
		{ TEXT("welcome"), TEXT("Welcome to the tutorial") }, { TEXT("round"), TEXT("How a round works") }, { TEXT("turn"), TEXT("Your turn") },
		{ TEXT("shipped"), TEXT("Your first shipment") }, { TEXT("post"), TEXT("A second trading post") }, { TEXT("fortune"), TEXT("Fortune cards") },
		{ TEXT("arrival"), TEXT("The plague arrives") }, { TEXT("chronicle"), TEXT("Chronicle cards") }, { TEXT("event"), TEXT("Event cards") },
		{ TEXT("decision"), TEXT("A card needs your decision") }, { TEXT("danger"), TEXT("Your family is in danger") }, { TEXT("survival"), TEXT("Plague & upkeep") },
		{ TEXT("legacy"), TEXT("Keep all three healthy") }, { TEXT("aftermath"), TEXT("Aftermath") }, { TEXT("more"), TEXT("More you can do") }, { TEXT("final"), TEXT("The end of the game") },
	};
	const FString* Found = Titles.Find(Id);
	return TEXT("Tutorial: ") + (Found ? *Found : FString(TEXT("a lesson")));
}
