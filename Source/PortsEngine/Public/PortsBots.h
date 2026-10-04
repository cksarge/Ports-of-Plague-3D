// Computer players (bots.js) and whole computer-played games (sim.js).
// Bots only use what a human player can see on screen: city states,
// "threatened" warnings, scores and the cards in play.
#pragma once

#include "CoreMinimal.h"
#include "PortsRules.h"

// The current house's next move, chosen one at a time so the game screen can
// show each move like a human's.
struct FPortsBotMove
{
	enum class EType : uint8 { Decide, Act, End };
	EType type = EType::End;
	EPortsChoice choice = EPortsChoice::No;
	FPortsAction action;
};

namespace Ports
{
	// How often a bot of this skill makes a "mistake" (0 = never).
	PORTSENGINE_API double MistakeRate(const FPortsPlayer& P);
	PORTSENGINE_API FPortsBotMove BotMove(FPortsState& State);
	// Answers the first waiting card with the bot's choice; if the engine
	// refuses it (it cannot pay, say), declines instead.
	PORTSENGINE_API FPortsResult BotDecide(FPortsState& State, EPortsChoice Choice);
	// The answer a house's playing style gives to the first card waiting for it.
	PORTSENGINE_API EPortsChoice BotChooseDecision(FPortsState& State, FPortsPlayer& P);
	// Plays the current player's whole turn at once (simulator and tests).
	PORTSENGINE_API FPortsResult PlayTurn(FPortsState& State);
	// Plays a whole game with computer players. Returns the number of turns taken, or -1 if it did not finish.
	PORTSENGINE_API int32 PlayBotGame(const FPortsSetup& Setup, FPortsState& OutState);
}
