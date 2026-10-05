#include "PortsNet.h"

#include "PortsData.h"
#include "PortsRules.h"
#include "PortsState.h"
#include "IWebSocket.h"
#include "Misc/ConfigCacheIni.h"
#include "WebSocketsModule.h"

using V = FPortsValue;

namespace
{
	const TCHAR* const Section = TEXT("PortsOfPlague.Net");
	FString Setting(const TCHAR* Key)
	{
		FString Value;
		GConfig->GetString(Section, Key, Value, GGameIni);
		return Value;
	}

	// protocol.js: the heartbeats, and how long before a silent device counts as off line.
	constexpr double PingEvery = 8.0;
	constexpr double OfflineAfter = 25.0;
	// Room codes: 4 letters and numbers, without the ones easy to mix up and without vowels.
	const TCHAR* const CodeChars = TEXT("BCDFGHJKMNPQRSTVWXZ23456789");
	constexpr int32 CodeLength = 4;
	constexpr int32 LogKept = 15;

	FString Str(const V& Message, const TCHAR* Key) { return Message.Get(Key).IsString() ? Message.Get(Key).AsString() : FString(); }
}

// ---------- The message pipe ----------

FPortsTransport::~FPortsTransport()
{
	Close();
}

bool FPortsTransport::IsAvailable()
{
	return !Setting(TEXT("SupabaseUrl")).IsEmpty() && !Setting(TEXT("SupabaseKey")).IsEmpty();
}

FString FPortsTransport::JoinAddress()
{
	return Setting(TEXT("JoinAddress"));
}

void FPortsTransport::Open(const FString& Code, TFunction<void(bool)> OnOpen)
{
	Topic = FString::Printf(TEXT("realtime:pop-room-%s"), *Code);
	Opened = MoveTemp(OnOpen);
	GiveUpAt = FPlatformTime::Seconds() + 12.0;
	Connect();
}

// One WebSocket to Supabase Realtime, speaking its (Phoenix) channel messages:
// { topic, event, payload, ref, join_ref }.
void FPortsTransport::Connect()
{
	if (bClosed) return;
	bJoined = false;
	FString Url = Setting(TEXT("SupabaseUrl"));
	Url.RemoveFromEnd(TEXT("/"));
	Url = Url.Replace(TEXT("https://"), TEXT("wss://")).Replace(TEXT("http://"), TEXT("ws://"));
	if (FParse::Param(FCommandLine::Get(), TEXT("PortsNetBad"))) Url = TEXT("wss://no-such-host.invalid");
	Url += FString::Printf(TEXT("/realtime/v1/websocket?apikey=%s&vsn=1.0.0"), *Setting(TEXT("SupabaseKey")));
	UE_LOG(LogTemp, Display, TEXT("PortsNet: connecting to %s"), *Url.Left(Url.Find(TEXT("?"))));
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets"))) FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	Socket = FWebSocketsModule::Get().CreateWebSocket(Url, TArray<FString>());
	TWeakPtr<FPortsTransport> Weak = AsShared();
	Socket->OnConnected().AddLambda([Weak]()
	{
		const TSharedPtr<FPortsTransport> Self = Weak.Pin();
		if (!Self.IsValid() || Self->bClosed) return;
		// Join the room's channel: broadcast only, and our own messages do not come back to us.
		Self->JoinRef = FString::FromInt(++Self->Ref);
		Self->SendFrame(TEXT("phx_join"), V::Object({ { TEXT("config"), V::Object({
			{ TEXT("broadcast"), V::Object({ { TEXT("ack"), false }, { TEXT("self"), false } }) },
			{ TEXT("presence"), V::Object({ { TEXT("key"), V(TEXT("")) } }) },
			{ TEXT("postgres_changes"), V::Array() },
			{ TEXT("private"), false } }) } }), Self->Topic);
	});
	Socket->OnMessage().AddLambda([Weak](const FString& Text) { if (const TSharedPtr<FPortsTransport> Self = Weak.Pin()) Self->HandleText(Text); });
	const auto Lost = [Weak]()
	{
		const TSharedPtr<FPortsTransport> Self = Weak.Pin();
		if (!Self.IsValid() || Self->bClosed) return;
		Self->bJoined = false;
		// Try again in a few seconds (Tick); the first opening gives up after twelve.
		Self->RetryAt = FPlatformTime::Seconds() + 3.0;
	};
	Socket->OnConnectionError().AddLambda([Lost](const FString& Error) { UE_LOG(LogTemp, Warning, TEXT("PortsNet: connection error: %s"), *Error); Lost(); });
	Socket->OnClosed().AddLambda([Lost](int32 Status, const FString& Reason, bool) { UE_LOG(LogTemp, Display, TEXT("PortsNet: connection closed (%d %s)."), Status, *Reason); Lost(); });
	Socket->Connect();
}

void FPortsTransport::SendFrame(const TCHAR* Event, const V& Payload, const FString& ToTopic)
{
	if (!Socket.IsValid() || !Socket->IsConnected()) return;
	const bool bJoin = FCString::Strcmp(Event, TEXT("phx_join")) == 0;
	V Frame = V::Object({ { TEXT("topic"), V(ToTopic) }, { TEXT("event"), V(Event) }, { TEXT("payload"), Payload }, { TEXT("ref"), V(bJoin ? JoinRef : FString::FromInt(++Ref)) } });
	if (ToTopic == Topic) Frame.Set(TEXT("join_ref"), V(JoinRef));
	Socket->Send(Frame.ToJson());
}

void FPortsTransport::HandleText(const FString& Text)
{
	V Frame;
	if (bClosed || !V::Parse(Text, Frame) || !Frame.IsObject()) return;
	const FString Event = Str(Frame, TEXT("event"));
	const V& Payload = Frame.Get(TEXT("payload"));
	if (Event == TEXT("phx_reply") && Str(Frame, TEXT("topic")) == Topic && Str(Frame, TEXT("ref")) == JoinRef)
	{
		const bool bOk = Str(Payload, TEXT("status")) == TEXT("ok");
		if (bOk)
		{
			bJoined = true;
			if (bEverJoined) UE_LOG(LogTemp, Display, TEXT("PortsNet: connection restored."));
			NextBeat = FPlatformTime::Seconds() + 25.0;
			const bool bAgain = bEverJoined;
			bEverJoined = true;
			if (Opened) { TFunction<void(bool)> Tell = MoveTemp(Opened); Opened = nullptr; Tell(true); }
			else if (bAgain && OnRejoined) OnRejoined();
		}
		else UE_LOG(LogTemp, Warning, TEXT("PortsNet: the room could not be joined: %s"), *Payload.ToJson());
	}
	else if (Event == TEXT("broadcast") && Str(Payload, TEXT("event")) == TEXT("m"))
	{
		if (OnMessage) OnMessage(Payload.Get(TEXT("payload")));
	}
	else if (Event == TEXT("phx_error") || Event == TEXT("phx_close"))
	{
		bJoined = false;
		RetryAt = FPlatformTime::Seconds() + 3.0;
	}
}

void FPortsTransport::Send(const V& Message)
{
	if (!bJoined) return;
	SendFrame(TEXT("broadcast"), V::Object({ { TEXT("type"), V(TEXT("broadcast")) }, { TEXT("event"), V(TEXT("m")) }, { TEXT("payload"), Message } }), Topic);
}

void FPortsTransport::Tick(double Now)
{
	if (bClosed) return;
	if (Opened && Now > GiveUpAt)
	{
		TFunction<void(bool)> Tell = MoveTemp(Opened);
		Opened = nullptr;
		Close();
		Tell(false);
		return;
	}
	if (bJoined && Now >= NextBeat)
	{
		NextBeat = Now + 25.0;
		SendFrame(TEXT("heartbeat"), V::Object(), TEXT("phoenix"));
	}
	if (!bJoined && RetryAt > 0 && Now >= RetryAt)
	{
		RetryAt = 0;
		if (Socket.IsValid()) { Socket->OnClosed().Clear(); Socket->OnConnectionError().Clear(); Socket->Close(); }
		Connect();
	}
}

void FPortsTransport::TestDrop()
{
	UE_LOG(LogTemp, Display, TEXT("PortsNet: connection cut on purpose (a test)."));
	if (Socket.IsValid()) Socket->Close();
}

void FPortsTransport::Close()
{
	if (bClosed) return;
	bClosed = true;
	bJoined = false;
	if (Socket.IsValid())
	{
		Socket->OnClosed().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnMessage().Clear();
		Socket->Close();
		Socket.Reset();
	}
}

// ---------- The big screen's side of a room ----------

FString FPortsRoom::MakeCode()
{
	FString Out;
	const int32 N = FCString::Strlen(CodeChars);
	for (int32 i = 0; i < CodeLength; i++) Out.AppendChar(CodeChars[FMath::RandHelper(N)]);
	return Out;
}

void FPortsRoom::Open(const FString& WantedCode, const TArray<FPortsSeat>& SavedSeats, bool bAlreadyStarted, TFunction<void(bool)> OnReady)
{
	Wanted = WantedCode;
	Seats = SavedSeats;
	for (FPortsSeat& S : Seats) { S.online = S.bot; S.lastSeen = 0; S.rid = 0; }
	bStarted = bAlreadyStarted;
	Ready = MoveTemp(OnReady);
	TryCode(0);
}

// Two big screens must never share a room: before a code is used, ask whether a host already answers on it.
void FPortsRoom::TryCode(int32 Tries)
{
	ProbeTries = Tries;
	Code = Wanted.IsEmpty() ? MakeCode() : Wanted;
	Transport = MakeShared<FPortsTransport>();
	TWeakPtr<FPortsRoom> Weak = AsShared();
	Transport->OnMessage = [Weak](const V& Message) { if (const TSharedPtr<FPortsRoom> Self = Weak.Pin()) Self->Receive(Message); };
	Transport->OnRejoined = [Weak]() { if (const TSharedPtr<FPortsRoom> Self = Weak.Pin()) { Self->Transport->Send(V::Object({ { TEXT("t"), V(TEXT("roll-call")) } })); Self->Resend(); } };
	Transport->Open(Code, [Weak](bool bOk)
	{
		const TSharedPtr<FPortsRoom> Self = Weak.Pin();
		if (!Self.IsValid() || Self->bClosed) return;
		if (!bOk)
		{
			if (Self->Ready) { TFunction<void(bool)> Tell = MoveTemp(Self->Ready); Self->Ready = nullptr; Tell(false); }
			return;
		}
		Self->bProbing = true;
		Self->bInUse = false;
		Self->ProbeUntil = FPlatformTime::Seconds() + 0.9;
		Self->Transport->Send(V::Object({ { TEXT("t"), V(TEXT("probe")) } }));
	});
}

void FPortsRoom::Tick(double Now)
{
	Clock = Now;
	if (bClosed || !Transport.IsValid()) return;
	Transport->Tick(Now);
	if (bProbing && (bInUse || Now >= ProbeUntil))
	{
		bProbing = false;
		if (bInUse)
		{
			UE_LOG(LogTemp, Display, TEXT("PortsNet: room %s is already in use by another big screen."), *Code);
			// Another big screen has this code: try another (a saved game's own code is given up too).
			Transport->Close();
			Wanted.Reset();
			if (ProbeTries >= 5) { if (Ready) { TFunction<void(bool)> Tell = MoveTemp(Ready); Ready = nullptr; Tell(false); } return; }
			TryCode(ProbeTries + 1);
			return;
		}
		bReady = true;
		NextWatch = Now + 4.0;
		NextBeat = Now + PingEvery;
		UE_LOG(LogTemp, Display, TEXT("PortsNet: room %s is open."), *Code);
		// Devices still on the page from before (a reopened saved game) report in now.
		Transport->Send(V::Object({ { TEXT("t"), V(TEXT("roll-call")) } }));
		if (Ready) { TFunction<void(bool)> Tell = MoveTemp(Ready); Ready = nullptr; Tell(true); }
		return;
	}
	if (!bReady) return;
	// Devices that stopped sending heartbeats are marked off line.
	if (Now >= NextWatch)
	{
		NextWatch = Now + 4.0;
		bool bChanged = false;
		for (FPortsSeat& S : Seats) if (S.online && !S.bot && Now - S.lastSeen > OfflineAfter) { S.online = false; bChanged = true; }
		if (bChanged) { if (OnChange) OnChange(); if (!bStarted) PushLobby(); }
	}
	// A heartbeat, so devices notice if this screen disappears without saying goodbye.
	if (Now >= NextBeat)
	{
		NextBeat = Now + PingEvery;
		Transport->Send(V::Object({ { TEXT("t"), V(TEXT("beat")) } }));
	}
}

void FPortsRoom::Close()
{
	if (bClosed) return;
	bClosed = true;
	if (Transport.IsValid())
	{
		if (bReady) Transport->Send(V::Object({ { TEXT("t"), V(TEXT("closed")) } }));
		// The goodbye is on its way before the pipe shuts.
		const TSharedPtr<FPortsTransport> Keep = Transport;
		Transport.Reset();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Keep](float) { Keep->Close(); return false; }), 0.3f);
	}
}

V FPortsRoom::PublicSeats() const
{
	V Out = V::Array();
	for (const FPortsSeat& S : Seats)
	{
		Out.Add(V::Object({ { TEXT("cid"), V(S.cid) }, { TEXT("name"), V(S.name) }, { TEXT("home"), V(S.home) }, { TEXT("left"), S.left }, { TEXT("bot"), S.bot },
			{ TEXT("skill"), S.skill.IsEmpty() ? V::Null() : V(S.skill) }, { TEXT("online"), S.online }, { TEXT("rid"), S.rid } }));
	}
	return Out;
}

V FPortsRoom::SavedSeatsValue() const
{
	V Out = V::Array();
	for (const FPortsSeat& S : Seats)
	{
		Out.Add(V::Object({ { TEXT("cid"), V(S.cid) }, { TEXT("name"), V(S.name) }, { TEXT("home"), V(S.home) }, { TEXT("left"), S.left }, { TEXT("bot"), S.bot }, { TEXT("skill"), S.skill.IsEmpty() ? V::Null() : V(S.skill) } }));
	}
	return Out;
}

TArray<FPortsSeat> FPortsRoom::SeatsFromValue(const V& Saved)
{
	TArray<FPortsSeat> Out;
	for (const V& S : Saved.GetItems())
	{
		FPortsSeat Seat;
		Seat.cid = Str(S, TEXT("cid")); Seat.name = Str(S, TEXT("name")); Seat.home = Str(S, TEXT("home")); Seat.skill = Str(S, TEXT("skill"));
		Seat.left = S.Get(TEXT("left")).Truthy(); Seat.bot = S.Get(TEXT("bot")).Truthy();
		Out.Add(Seat);
	}
	return Out;
}

void FPortsRoom::PushLobby()
{
	if (bReady && Transport.IsValid()) Transport->Send(V::Object({ { TEXT("t"), V(TEXT("lobby")) }, { TEXT("seats"), PublicSeats() }, { TEXT("options"), Options } }));
}

void FPortsRoom::PushState(const V& State, const V& View)
{
	if (!bReady || !Transport.IsValid()) return;
	// trimState: only the end of the long log goes to the devices.
	V Trimmed = State;
	if (V* Log = Trimmed.Find(TEXT("log")))
	{
		while (Log->Num() > LogKept) Log->RemoveAt(0);
	}
	Last = V::Object({ { TEXT("t"), V(TEXT("state")) }, { TEXT("rev"), ++Rev }, { TEXT("state"), Trimmed }, { TEXT("view"), View }, { TEXT("seats"), PublicSeats() } });
	Transport->Send(Last);
}

// What a device needs when it reports in: the game as it stands, or the lobby.
void FPortsRoom::Resend()
{
	if (bStarted && Last.IsObject())
	{
		Last.Set(TEXT("seats"), PublicSeats());
		Transport->Send(Last);
	}
	else PushLobby();
}

void FPortsRoom::Handled(const V& Message)
{
	const FString From = Str(Message, TEXT("from"));
	if (!Message.Get(TEXT("rid")).IsNumber()) return;
	for (FPortsSeat& S : Seats) if (S.cid == From) S.rid = FMath::Max(S.rid, Message.Get(TEXT("rid")).AsNumber());
}

void FPortsRoom::Toast(int32 Seat, const FString& Text)
{
	if (Seats.IsValidIndex(Seat) && bReady && Transport.IsValid()) Transport->Send(V::Object({ { TEXT("t"), V(TEXT("toast")) }, { TEXT("to"), V(Seats[Seat].cid) }, { TEXT("text"), V(Text) } }));
}

void FPortsRoom::RemoveSeat(int32 Index)
{
	if (!Seats.IsValidIndex(Index)) return;
	Seats.RemoveAt(Index);
	PushLobby();
	if (OnChange) OnChange();
}

// A computer house, played on the big screen. Its id can never match a device's.
void FPortsRoom::AddBot(const FString& Name, const FString& Home, const FString& Skill)
{
	FPortsSeat S;
	S.cid = TEXT("bot:") + Name; S.name = Name; S.home = Home; S.bot = true; S.skill = Skill; S.online = true;
	Seats.Add(S);
	PushLobby();
	if (OnChange) OnChange();
}

void FPortsRoom::SetSkill(int32 Index, const FString& Skill)
{
	if (!Seats.IsValidIndex(Index) || !Seats[Index].bot) return;
	Seats[Index].skill = Skill;
	PushLobby();
	if (OnChange) OnChange();
}

void FPortsRoom::Receive(const V& M)
{
	if (bClosed || !M.IsObject()) return;
	const FString T = Str(M, TEXT("t"));
	if (bProbing) { if (T == TEXT("host-here")) bInUse = true; return; }
	if (!bReady) return;
	if (T == TEXT("probe")) { Transport->Send(V::Object({ { TEXT("t"), V(TEXT("host-here")) } })); return; }
	const FString From = Str(M, TEXT("from"));
	const int32 i = Seats.IndexOfByPredicate([&From](const FPortsSeat& S) { return S.cid == From && !S.bot; });
	if (i != INDEX_NONE && T != TEXT("leave"))
	{
		// Any message from a house that had left (the same device joining the room again) brings it back.
		FPortsSeat& Seat = Seats[i];
		const bool bChanged = !Seat.online || Seat.left;
		Seat.online = true;
		Seat.left = false;
		Seat.lastSeen = Clock;
		if (bChanged && OnChange) OnChange();
	}
	if (T == TEXT("hello")) Resend();
	else if (T == TEXT("ping")) { if (bStarted && Last.IsObject() && M.Get(TEXT("rev")).AsInt(-1) != Rev) Resend(); }
	else if (T == TEXT("join")) OnJoin(M, i);
	else if (T == TEXT("leave"))
	{
		if (i != INDEX_NONE && !bStarted) RemoveSeat(i);
		else if (i != INDEX_NONE) { Seats[i].online = false; Seats[i].left = true; if (OnChange) OnChange(); }
	}
	else if (T == TEXT("act") || T == TEXT("decide") || T == TEXT("end") || T == TEXT("next"))
	{
		if (bStarted && OnIntent) OnIntent(M);
	}
}

void FPortsRoom::OnJoin(const V& M, int32 i)
{
	const FString From = Str(M, TEXT("from"));
	const FString Name = Str(M, TEXT("name")).TrimStartAndEnd();
	const auto Reject = [this, &From](const FString& Reason) { Transport->Send(V::Object({ { TEXT("t"), V(TEXT("reject")) }, { TEXT("to"), V(From) }, { TEXT("reason"), V(Reason) } })); };
	if (bStarted)
	{
		// A device that lost its place (new browser, cleared data) takes its house back by typing the same house name.
		const int32 j = Seats.IndexOfByPredicate([&Name](const FPortsSeat& S) { return !S.bot && S.name.Equals(Name, ESearchCase::IgnoreCase); });
		if (j == INDEX_NONE) { Reject(TEXT("This game has already started. To rejoin, type your house name exactly as before.")); return; }
		Seats[j].cid = From; Seats[j].left = false; Seats[j].online = true; Seats[j].lastSeen = Clock;
		if (OnChange) OnChange();
		Resend();
		return;
	}
	TArray<FPortsSeat> Others = Seats;
	if (i != INDEX_NONE) Others.RemoveAt(i);
	const FString Home = Str(M, TEXT("home"));
	const FString Problem = PortsNet::ValidateJoin(Others, Name, Home);
	if (!Problem.IsEmpty()) { Reject(Problem); return; }
	if (i != INDEX_NONE) { Seats[i].name = Name; Seats[i].home = Home; }
	else
	{
		FPortsSeat S;
		S.cid = From; S.name = Name; S.home = Home; S.online = true; S.lastSeen = Clock;
		Seats.Add(S);
	}
	PushLobby();
	if (OnChange) OnChange();
}

// ---------- Checks ----------

FString PortsNet::ValidateJoin(const TArray<FPortsSeat>& Others, const FString& Name, const FString& Home)
{
	const FPortsData& Data = FPortsData::Get();
	const int32 Max = Data.Int(TEXT("players.max"));
	if (Name.TrimStartAndEnd().IsEmpty()) return TEXT("Every house needs a name.");
	if (Name.TrimStartAndEnd().Len() > 24) return TEXT("House names can be at most 24 letters.");
	if (!Data.HomeCities.Contains(Home)) return TEXT("Choose one of the home cities.");
	if (Others.Num() >= Max) return FString::Printf(TEXT("The game is full (%d houses)."), Max);
	for (const FPortsSeat& S : Others) if (S.home == Home) return FString::Printf(TEXT("%s already has that home city. Choose another."), *S.name);
	return FString();
}

FString PortsNet::ValidateIntent(const FPortsState& State, const TArray<FPortsSeat>& Seats, const V& M)
{
	if (!M.IsObject()) return TEXT("Unknown message.");
	const FString From = Str(M, TEXT("from")), T = Str(M, TEXT("t"));
	const int32 Seat = Seats.IndexOfByPredicate([&From](const FPortsSeat& S) { return S.cid == From && !S.bot; });
	if (Seat == INDEX_NONE) return TEXT("This device has not joined the game.");
	if (T == TEXT("next")) return M.Get(TEXT("id")).IsNumber() ? FString() : FString(TEXT("Unknown card."));
	if (T != TEXT("act") && T != TEXT("decide") && T != TEXT("end")) return TEXT("Unknown message.");
	const FPortsPlayer* P = Ports::CurrentPlayer(State);
	if (!P) return TEXT("It is not a player’s turn.");
	if (P->id != Seat) return FString::Printf(TEXT("It is %s's turn."), *P->name);
	if (T == TEXT("act"))
	{
		const V& A = M.Get(TEXT("action"));
		const FString Type = Str(A, TEXT("type"));
		const bool bKnown = A.IsObject() && FPortsData::Get().Actions().GetItems().ContainsByPredicate([&Type](const V& X) { return X.Get(TEXT("id")).AsString() == Type; });
		if (!bKnown) return TEXT("Unknown action.");
		if (P->pending.Num()) return TEXT("First answer the card waiting for you.");
	}
	if (T == TEXT("decide"))
	{
		if (!P->pending.Num()) return TEXT("There is no decision waiting.");
		const V& C = M.Get(TEXT("choice"));
		if (!(C.IsBool() || (C.IsString() && (C.AsString() == TEXT("obey") || C.AsString() == TEXT("pay"))))) return TEXT("Unknown choice.");
	}
	if (T == TEXT("end") && P->pending.Num()) return TEXT("First answer the card waiting for you.");
	return FString();
}
