// Music and sound effects. The songs are the web version's six recordings and play at the
// same moments (see the notes at the top of music.js there):
//
//   menu      title screen, new-game screen, and the prologue and turn order of a game;
//             stops when the first round's opening card appears
//   trade-1   start with a round's opening card and play through its cards and turns;
//   trade-2   stop when that round's plague results appear (which of the three depends
//   trade-3   on the year: rounds 1-3, 4-7, and 8 onward)
//   plague    only while a round's plague results are on screen: starts when they open,
//             stops when the last of them is closed
//   ending    starts when the game is over and plays through the finale and the results
//             page; stops on returning to the menu or starting a new game
//
// Starting and stopping are not done at those moments one by one. Instead SongWanted()
// answers, from how the game stands right now, which single song should be heard, and
// TickAudio() makes that true every frame: the wanted song fades in, and every other song
// fades out and is paused. So a song cannot be left playing by a missed "stop": the
// moment SongWanted() no longer names it, it goes.
#include "PortsGameFlow.h"

#include "PortsData.h"
#include "PortsFinale.h"
#include "PortsMapActor.h"
#include "PortsSettings.h"
#include "SPortsRoot.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"

namespace
{
	const TCHAR* const SongNames[] = { TEXT("menu"), TEXT("trade-1"), TEXT("trade-2"), TEXT("trade-3"), TEXT("plague"), TEXT("ending") };
	const TCHAR* const EffectNames[] = { TEXT("click"), TEXT("dice"), TEXT("bell"), TEXT("knell"), TEXT("page"), TEXT("coin"), TEXT("sail"), TEXT("cart"), TEXT("fortune"), TEXT("misfortune"),
		TEXT("plague"), TEXT("fanfare"), TEXT("victory"), TEXT("drumroll"), TEXT("stamp"), TEXT("error"), TEXT("low") };
	// Recorded music sits under the sound effects (VOLUME in music.js), and changes over a second and a half (FADE_MS).
	constexpr float SongVolume = 0.5f;
	constexpr float FadeSeconds = 1.5f;
	const TCHAR* const Settings = TEXT("PortsOfPlague.Sound");
}

void UPortsGameFlow::LoadAudio()
{
	for (const TCHAR* Song : SongNames)
	{
		const FString Asset = FString(TEXT("music_")) + FString(Song).Replace(TEXT("-"), TEXT("_"));
		if (USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Ports/Audio/%s.%s"), *Asset, *Asset))) SoundAssets.Add(FName(Song), Loaded);
		else UE_LOG(LogTemp, Warning, TEXT("PortsMusic: the song '%s' is missing. Run Tools/build_content.sh."), Song);
		SongLevel.Add(FName(Song), 0.f);
	}
	for (const TCHAR* Effect : EffectNames)
	{
		const FString Asset = FString(TEXT("sfx_")) + Effect;
		if (USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Ports/Audio/%s.%s"), *Asset, *Asset))) SoundAssets.Add(FName(*Asset), Loaded);
	}
	// The two switches are remembered between games, as the web version remembers them in the browser.
	GConfig->GetBool(Settings, TEXT("SoundOn"), bSoundOn, GGameUserSettingsIni);
	GConfig->GetBool(Settings, TEXT("MusicOn"), bMusicOn, GGameUserSettingsIni);
}

void UPortsGameFlow::SetSoundOn(bool bOn)
{
	bSoundOn = bOn;
	GConfig->SetBool(Settings, TEXT("SoundOn"), bSoundOn, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UPortsGameFlow::SetMusicOn(bool bOn)
{
	bMusicOn = bOn;
	GConfig->SetBool(Settings, TEXT("MusicOn"), bMusicOn, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UPortsGameFlow::Sound(const TCHAR* Name, double AfterSeconds)
{
	if (!bSoundOn || bAutoPlay) return;
	if (AfterSeconds > 0) { LaterSounds.Add({ Now() + AfterSeconds, FName(Name) }); return; }
	if (const TObjectPtr<USoundBase>* Found = SoundAssets.Find(FName(*(FString(TEXT("sfx_")) + Name)))) UGameplayStatics::PlaySound2D(Map, *Found, 0.9f * FPortsSettings::Get().SoundVolume);
}

// tradeMood and gameSong in music.js.
FString UPortsGameFlow::GameSong() const
{
	if (State.phase == TEXT("ended")) return TEXT("ending");
	if (State.round < State.firstHalf) return TEXT("menu");
	return State.round <= 3 ? TEXT("trade-1") : State.round <= 7 ? TEXT("trade-2") : TEXT("trade-3");
}

FString UPortsGameFlow::SongWanted() const
{
	// Music switched off: nothing.
	if (!bMusicOn) return FString();
	// The splash screen is silent; the menu's song begins with the menu.
	if (SplashUntil > 0) return FString();
	// The game is over: the ending song, through the finale and the results page.
	if (Finale.IsValid() || bResults) return TEXT("ending");
	// Not in a game (title screen, new-game screen): the menu song.
	if (!bInGame) return TEXT("menu");
	// A round's plague results are open on screen: the plague song, and only then.
	if (bPlagueCard && Root.IsValid() && Root->HasDialog()) return TEXT("plague");
	// Otherwise the song chosen for this round.
	return RoundSong;
}

void UPortsGameFlow::TickAudio(float DeltaSeconds)
{
	if (!Map) return;
	// Sounds that were asked for a moment ahead.
	for (int32 i = LaterSounds.Num() - 1; i >= 0; i--)
	{
		if (Now() < LaterSounds[i].At) continue;
		const FName Name = LaterSounds[i].Name;
		LaterSounds.RemoveAt(i);
		Sound(*Name.ToString());
	}

	const FString Wanted = SongWanted();
	if (Wanted != SongHeard)
	{
		UE_LOG(LogTemp, Display, TEXT("PortsMusic: %s -> %s"), SongHeard.IsEmpty() ? TEXT("(silence)") : *SongHeard, Wanted.IsEmpty() ? TEXT("(silence)") : *Wanted);
		SongHeard = Wanted;
	}
	const float Step = DeltaSeconds / FadeSeconds;
	int32 Sounding = 0;
	for (const TCHAR* Song : SongNames)
	{
		const FName Id(Song);
		float& Level = SongLevel.FindOrAdd(Id);
		const bool bWanted = Wanted == Song;
		Level = bWanted ? FMath::Min(1.f, Level + Step) : FMath::Max(0.f, Level - Step);
		TObjectPtr<UAudioComponent>& Player = Songs.FindOrAdd(Id);
		if (Level > 0.f)
		{
			if (!Player)
			{
				const TObjectPtr<USoundBase>* Asset = SoundAssets.Find(Id);
				if (!Asset) continue;
				Player = UGameplayStatics::CreateSound2D(Map, *Asset, 1.f, 1.f, 0.f, nullptr, false, false);
				if (!Player) continue;
				Player->bIsUISound = true;
				Player->Play();
			}
			// A song that was stopped earlier picks up where it left off.
			if (Player->bIsPaused) Player->SetPaused(false);
			if (!Player->IsPlaying()) Player->Play();
			Player->SetVolumeMultiplier(FMath::Max(0.001f, Level * SongVolume * FPortsSettings::Get().MusicVolume));
			Sounding++;
		}
		else if (Player && !Player->bIsPaused)
		{
			// Silent and not wanted: paused, so that it is truly off, not merely quiet.
			Player->SetPaused(true);
			UE_LOG(LogTemp, Display, TEXT("PortsMusic: %s stopped"), Song);
		}
	}
	// More than one song is only ever heard during the second and a half of a change from one to the next (three, if two changes come within that time).
	if (Sounding > 3) UE_LOG(LogTemp, Warning, TEXT("PortsMusic: %d songs are sounding at once."), Sounding);
}
