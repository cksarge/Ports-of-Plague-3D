// Runs a game on one screen: the menu and setup, then the same sequence as the
// web version's game screen (src/ui/game.js). It shows each new entry of the
// engine's log as a card, hands the device from house to house, lets the
// current house choose actions and answer cards, plays the bots, runs the
// turn timer, saves after every change, and ends with the final scores.
#pragma once

#include "CoreMinimal.h"
#include "PortsBots.h"
#include "UObject/Object.h"
#include "PortsGameFlow.generated.h"

class APortsMapActor;
class SPortsRoot;
class SWidget;
class FPortsDoc;
struct FPortsDialogOptions;

// What the interface remembers besides the game itself (saved with it).
struct FPortsUiState
{
	bool hints = false;
	// History Mode (not on the website): false = the historical notes, the Journal and the real history at the end are left out.
	bool history = true;
	// The tutorial (not on the website): a practice game with lesson cards, and the lessons already given.
	bool tutorial = false;
	TArray<FString> lessons;
	// The last log entry already shown to the players.
	int32 seenSeq = 0;
	// The facts of the latest historical note.
	TArray<FString> lastNote;
	// A multi-device game: its room code and seats, so that the room can be opened again from a saved game.
	FString roomCode;
	FPortsValue roomSeats;
};

UCLASS()
class PORTSOFPLAGUE_API UPortsGameFlow : public UObject
{
	GENERATED_BODY()

public:
	void Start(APortsMapActor* InMap);
	void Stop();
	void Tick(float DeltaSeconds);

	// True while the pointer is on a panel or card, so the map should not react to it.
	bool IsPointerOverUi() const;
	// A click on the map (in map pixels) that was not a drag.
	void OnMapClicked(const FVector2D& Pixel);

	bool IsInGame() const { return bInGame; }
	const FPortsState& GetState() const { return State; }

	// For checking the game from the command line: starts a game straight away.
	void StartTestGame(const FString& Spec);
	// For checking a redraw: changes a setting on the setup screen at the very moment a picture is taken.
	void TestBeforeShot();

private:
	// ---------- Screens (PortsFlowScreens.cpp) ----------
	// bArrive: the menu's card rises into place (coming from the start screen).
	void ShowMenu(bool bArrive = false);
	// The start screen: the title and a Start button over the blurred map, which cannot be moved until Start is pressed.
	void ShowStart(const TSharedPtr<SWidget>& Over = nullptr);
	void LeaveStart();
	void ShowSetup();
	void BeginGame(const FPortsSetup& Setup, bool bHints, bool bHistory = true);
	void ContinueSaved();
	void EnterGame();
	void LeaveGame();
	void ShowEnd();
	void ShowRules();
	void ShowJournal();
	void ShowCredits();
	void ShowResearch();
	void ShowLicenses();
	void ShowCity(const FString& CityId);
	TSharedRef<SWidget> BuildGameScreen();
	void Refresh();
	void RefreshTopBar();
	void StartClock();
	double Now() const;
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildSidebar();
	TSharedRef<SWidget> BuildSidebarWith(int32 LogLines, bool bNote, float Zoom);

	// ---------- Saving ----------
	FString SavePath() const;
	void Save();
	bool LoadSaved(FPortsState& OutState, FPortsUiState& OutUi) const;
	void ClearSave();

	// ---------- The sequence of play (PortsGameFlow.cpp) ----------
	void Pump();
	bool PresentNew();
	void MarkSeen(int32 Seq);
	void SetNote(const FPortsValue& FactIds);
	void BeginTurn();
	void StartAction(const FString& Id);
	void RunAction(const FPortsAction& Action);
	void ShowActionResult(const FPortsValue& Entry);
	void ApplyDecision(EPortsChoice Choice);
	void TryEndTurn();
	void FinishTurn();
	void BotStep();
	void ExpireTurn();
	void Notify(const FString& Text, float Seconds = 3.2f);
	bool OnePerson() const;
	bool Busy() const;
	void HandleKeys();

	// ---------- Cards (PortsFlowStories.cpp) ----------
	// Queues a story card (prologue, round start, Chronicle, Event, Fortune, plague results...),
	// built from the same kind and data the web version uses. After runs when the last page closes.
	void Story(const FString& Kind, const FPortsValue& Data, TFunction<void()> After = nullptr);
	void BuildStory(const FString& Kind, const FPortsValue& Data, FPortsDoc& Doc, FString& Button, FPortsDialogOptions& Options) const;
	void OpenStoryPage(const FString& Kind, const FPortsValue& Data);
	TArray<FPortsValue> StoryPages(const FString& Kind, const FPortsValue& Data) const;
	float StoryZoom(const FString& Kind, const FPortsValue& Page) const;

	// ---------- The tutorial (PortsFlowTutorial.cpp; its screens are in PortsFlowScreens.cpp) ----------
	void ShowTutorialChoice();
	void ShowTutorialLobby();
	// OwnDevice: the seat that joined the tutorial's room, or null to play on this screen.
	void BeginTutorial(const struct FPortsSeat* OwnDevice);
	bool Lesson(const FString& Id);
	bool TutorialBefore();
	bool TutorialTurn(const FPortsPlayer& P);
	void BuildLesson(const FString& Id, FPortsDoc& Doc, FString& Button) const;
	FString LessonTitle(const FString& Id) const;
	bool bTutorialLobby = false;
	FString TutorialLobbyError;

	// ---------- Choices (PortsFlowPrompts.cpp) ----------
	void OpenActionPrompt(const FString& Id);
	void OpenMovePrompt();
	void OpenDecisionPrompt();
	void OpenEndTurnPrompt();
	FString QuickBlock(const FString& Id, const FPortsPlayer& P) const;
	FString HintFor(const FPortsPlayer& P) const;
	void AddHousePanel(FPortsDoc& Doc, const FPortsPlayer& P) const;
	void AddActionsPanel(FPortsDoc& Doc, const FPortsPlayer& P);
	// Opens a card or choice; when it closes, OnClose runs and play goes on.
	void Open(TFunction<TSharedRef<SWidget>(TFunction<void(const FString&)>)> Build, const FPortsDialogOptions& Options, TFunction<void(const FString&)> OnClose);

	UPROPERTY()
	TObjectPtr<APortsMapActor> Map;

	TSharedPtr<SPortsRoot> Root;

	FPortsState State;
	FPortsUiState Ui;
	bool bInGame = false;

	// Things waiting to be shown, in order. Each either opens a card (play waits for it) or is done at once.
	TArray<TFunction<void()>> Steps;
	bool bPumping = false;
	bool bPumpAgain = false;
	// The round and turn whose "pass the device" has been shown.
	int32 HandledTurn = -1;
	// A bot is thinking: its next move is on a timer.
	bool bBotWaiting = false;
	double BotMoveAt = 0;
	int32 BotMoves = 0;
	// The action picker that is open, and the cities that can be clicked on the map for it.
	FString OpenPrompt;
	TArray<FString> Selectable;
	TArray<FString> HighlightRoutes;
	TFunction<void(const FString&)> PromptCityChosen;
	// Redraws the choice that is open after one of its switches is pressed.
	TSharedPtr<TFunction<void()>> PromptRebuild;
	// The setup screen's choices so far.
	TSharedPtr<struct FPortsSetupForm> SetupForm;

	// Turn timer (when switched on): runs during a person's turn, stops while a card is on screen.
	bool bClockOn = false;
	double ClockLeft = 0;
	int32 ClockShown = -1;

	TSharedPtr<class SBox> TopBarSlot;
	TSharedPtr<class SBox> SidebarSlot;
	TSharedPtr<class SScrollBox> SideScroll;
	// Redraws the map legend when it is folded or opened.
	TSharedPtr<TFunction<void()>> LegendKeep;
	// The inside of the setup screen's frame, while that screen is showing.
	TWeakPtr<class SBox> SetupHolder;

	// Checking the game from the command line: every house is played by the computer and cards close by themselves.
	bool bAutoPlay = false;
	double AutoCloseAt = 0;

	// Camera work: the game's own camera moves, and short holds while something is shown on the map.
	class APortsCameraPawn* Camera() const;
	void LookAt(const TArray<FString>& CityIds, double MaxZoomIn);
	void LookAtWholeMap();
	// Nothing new is shown for this long; a click or a key ends it at once.
	void HoldFor(double Seconds);
	double HoldUntil = 0;
	// ---------- Multi-device play (PortsNet.h) ----------
	// The room being filled on the new-game screen, and the room of the game being played (if it is a multi-device game).
	TSharedPtr<class FPortsRoom> LobbyRoom;
	TSharedPtr<class FPortsRoom> Room;
	void OpenLobbyRoom();
	void CloseLobbyRoom();
	// What the players' devices need besides the state (view() in game.js).
	FPortsValue DeviceView() const;
	// Sends the game as it stands to the devices, a moment from now (several changes at once go as one message).
	void PushToDevices();
	// This is the big screen of a multi-device game: players choose on their own devices, not here.
	bool Remote() const { return Room.IsValid(); }
	// The house's player left the game on their device: it sits out until they rejoin.
	bool HasLeft(const struct FPortsPlayer* Player) const;
	// Opens a saved multi-device game's room again, then carries on with the game.
	void ReopenRoom();
	void AdoptRoom();
	// A request from a device: an action, an answer to a card, ending the turn, or Next on a story card.
	void HandleIntent(const FPortsValue& Message);
	// The story card on screen, which any device can read and press Next on.
	int32 StorySeq = 0, StoryId = 0;
	FString StoryTitle, StoryLabel, StoryKind;
	FPortsValue StoryData;
	double PushAt = 0;
	bool bLastHold = false;

	// ---------- Sound (PortsFlowAudio.cpp) ----------
	// Plays one of the web version's sound effects by name, now or a little later.
	void Sound(const TCHAR* Name, double AfterSeconds = 0);
	// The one place that says which song should be sounding at this moment ("" for none). Every other song is
	// faded out and paused by TickAudio, whatever happened before: a song can only be heard while this names it.
	FString SongWanted() const;
	// The trading song for the game as it stands (darker as the years pass), or the menu song before the first round.
	FString GameSong() const;
	void TickAudio(float DeltaSeconds);
	void LoadAudio();
	void SetSoundOn(bool bOn);
	void SetMusicOn(bool bOn);
	bool bSoundOn = true, bMusicOn = true;
	// The song chosen when the game was entered, a round began or a round's plague results were closed.
	FString RoundSong;
	// A round's plague results are on screen (the only time the plague song plays).
	bool bPlagueCard = false;
	// The results page is showing (the ending song plays on through it).
	bool bResults = false;
	struct FLaterSound { double At; FName Name; };
	TArray<FLaterSound> LaterSounds;
	// How loud each song is now, from 0 (silent, paused) to 1 (its full level).
	TMap<FName, float> SongLevel;
	FString SongHeard;

	UPROPERTY()
	TMap<FName, TObjectPtr<class UAudioComponent>> Songs;

	UPROPERTY()
	TMap<FName, TObjectPtr<class USoundBase>> SoundAssets;

	// The finale: the show before the results page (PortsFlowFinale.cpp).
	void ShowFinale();
	void FinaleShow(int32 Index);
	void FinaleTick(float DeltaSeconds);
	void FinaleFinish();
	TSharedPtr<struct FPortsFinaleModel> Finale;
	struct FFinaleShip { FString Route, From, To; FLinearColor Color; };
	TArray<FFinaleShip> FinaleShips;
	TArray<FString> FinaleEarly;
	int32 FinaleLaunched = 0, FinaleStamped = 0;
	double FinaleGap = 0;
	// Checking the finale: this scene is shown as soon as the game ends, and stays.
	int32 TestFinale = -1;
	bool bTestEarly = false;
	// Checking one moment of a scene: the scene stops this many seconds in.
	double TestFinaleAt = -1;

	// Checking whole games: the setup a self-playing game began from, so that its result can be compared with the
	// rules engine playing the same game by itself; and whether it saves to disk and loads itself back every round.
	TSharedPtr<struct FPortsSetup> TestSetup;
	bool bTestReload = false;
	int32 ReloadedRound = MIN_int32;
	void TestCheckResult();

	// Checking one action picker from the command line.
	FString TestPrompt;
	FString TestHold;
	bool bTestFlip = false;
	bool bTestScrollEnd = false;
	int32 MenuDueIn = 0;
	bool bWindowNamed = false;
	// The splash screen is up until this time (0: it is not).
	double SplashUntil = 0;
	bool bStartScreen = false;
	// When Start was pressed (0: not yet): the blur clears and the title fades from then.
	double StartLeftAt = 0;
	TSharedPtr<SWidget> StartContent;
	// After Start: the title takes this long to fade, and the map this long to come into focus.
	static constexpr double StartTitleSeconds = 0.6, StartClearSeconds = 1.5;
	float StartBlur() const;
	FString SplashStyle() const;
	bool bTestLobbyPlay = false, bTestLobbyTimer = false;
	int32 TestLobbyHumans = 1;
	FString TestLobbySpec;
	double TestDropAt = -1;
	void TestLobbyPlay();
	// A script of clicks and key presses from the command line (PortsFlowTest.cpp).
	TArray<FString> TestSteps;
	int32 TestStep = -1;
	double TestStepAt = 0, TestStepGiveUp = 0, TestStepPatience = 40;
	FString TestKeyUp;
	void TestScriptTick();
};
