// Multi-device play: this game as the "big screen" of a room that players join from the
// web version's page on their own phones, tablets or computers.
//
// FPortsTransport is one room's message pipe, the same one the web version uses (a Supabase
// Realtime broadcast channel named pop-room-<code>; src/net/transport.js). FPortsRoom is the
// big screen's side of the room, a port of src/net/host.js: it finds a free room code, keeps
// the list of seats, answers the devices, and hands their requests to the game. The messages
// themselves are those of src/net/protocol.js, unchanged.
#pragma once

#include "CoreMinimal.h"
#include "PortsValue.h"

class IWebSocket;
struct FPortsState;

class FPortsTransport : public TSharedFromThis<FPortsTransport>
{
public:
	~FPortsTransport();

	// Whether a Supabase project is set up for this copy of the game ([PortsOfPlague.Net] in DefaultGame.ini).
	static bool IsAvailable();
	static FString JoinAddress();

	// Joins the room's channel. OnOpen(true) once messages can be sent; OnOpen(false) if it could not be joined.
	void Open(const FString& Code, TFunction<void(bool)> OnOpen);
	void Send(const FPortsValue& Message);
	void Close();
	// Keeps the connection alive, and joins again if it was lost.
	void Tick(double Now);
	bool IsOpen() const { return bJoined; }
	// For checking the game: cuts the connection, as a lost network would.
	void TestDrop();

	TFunction<void(const FPortsValue&)> OnMessage;
	// The connection was lost and found again (the room should say where the game stands).
	TFunction<void()> OnRejoined;

private:
	void Connect();
	void SendFrame(const TCHAR* Event, const FPortsValue& Payload, const FString& Topic);
	void HandleText(const FString& Text);

	TSharedPtr<IWebSocket> Socket;
	FString Topic;
	TFunction<void(bool)> Opened;
	bool bJoined = false, bEverJoined = false, bClosed = false;
	int32 Ref = 0;
	FString JoinRef;
	double NextBeat = 0, RetryAt = 0, GiveUpAt = 0;
};

struct FPortsSeat
{
	FString cid, name, home, skill;
	bool left = false, bot = false, online = false;
	double lastSeen = 0;
	// The last request from this device that the game has finished handling. Devices number their requests with
	// the time in milliseconds, far too large for a 32-bit number.
	double rid = 0;
};

class FPortsRoom : public TSharedFromThis<FPortsRoom>
{
public:
	// Opens a room. With a code (a saved game's room) that code is used if it is free; otherwise codes
	// are tried until one is free. OnReady(false) if no room could be opened.
	void Open(const FString& WantedCode, const TArray<FPortsSeat>& SavedSeats, bool bAlreadyStarted, TFunction<void(bool)> OnReady);
	void Tick(double Now);
	void Close();

	void PushLobby();
	// The game as it stands, for the devices: the state (with only the end of its long log) and the view.
	void PushState(const FPortsValue& State, const FPortsValue& View);
	// A request from a device has been dealt with (the device stops showing it as waiting).
	void Handled(const FPortsValue& Message);
	void Toast(int32 Seat, const FString& Text);
	void RemoveSeat(int32 Index);
	void AddBot(const FString& Name, const FString& Home, const FString& Skill);
	void SetSkill(int32 Index, const FString& Skill);

	void TestDrop() { if (Transport.IsValid()) Transport->TestDrop(); }
	bool IsConnected() const { return Transport.IsValid() && Transport->IsOpen(); }
	FPortsValue PublicSeats() const;
	FPortsValue SavedSeatsValue() const;
	static TArray<FPortsSeat> SeatsFromValue(const FPortsValue& Saved);

	FString Code;
	bool bStarted = false;
	bool bReady = false;
	TArray<FPortsSeat> Seats;
	// { mode, difficulty, prePlague, timer }: shown to players waiting in the lobby.
	FPortsValue Options = FPortsValue::Object();
	// A seat was taken, given up, or went on or off line.
	TFunction<void()> OnChange;
	// act, decide, end or next from a device, once the game has started.
	TFunction<void(const FPortsValue&)> OnIntent;

private:
	void TryCode(int32 Tries);
	void Receive(const FPortsValue& Message);
	void OnJoin(const FPortsValue& Message, int32 Index);
	void Resend();
	static FString MakeCode();

	TSharedPtr<FPortsTransport> Transport;
	TFunction<void(bool)> Ready;
	FString Wanted;
	bool bProbing = false, bInUse = false, bClosed = false;
	double ProbeUntil = 0, NextWatch = 0, NextBeat = 0, Clock = 0;
	int32 Rev = 0, ProbeTries = 0;
	FPortsValue Last;
};

// The checks the big screen runs on a device's request (validateIntent and validateJoin in protocol.js).
namespace PortsNet
{
	// "" if the game should go ahead, otherwise a plain-language reason.
	FString ValidateIntent(const FPortsState& State, const TArray<FPortsSeat>& Seats, const FPortsValue& Message);
	FString ValidateJoin(const TArray<FPortsSeat>& Others, const FString& Name, const FString& Home);
}
