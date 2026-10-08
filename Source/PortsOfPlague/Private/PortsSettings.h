// The player's own settings for sound and the camera (not on the website), kept between sessions in the
// game's user settings file. Graphics settings are Unreal's own (UGameUserSettings).
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FPortsSettings
{
	// 0 to 1.
	float SoundVolume = 1.f;
	float MusicVolume = 1.f;
	// 0 slow, 1 normal, 2 fast.
	int32 MoveSpeed = 1;
	int32 ZoomSpeed = 1;
	// Moving the mouse up tips the camera the other way.
	bool bInvertTilt = false;

	// One thing a key can be set to do: its id (an action's id from actions.json, or one of the game's own), its
	// name in the Controls list, the key it has until the player changes it, and which part of the list it is in.
	struct FBinding { FName Id; const TCHAR* Name; FKey Default; int32 Group; };
	static const TArray<FBinding>& Bindings();
	// The key that does this now.
	FKey Key(FName Id) const;
	// The key's name as shown on buttons and in hints ("1", "W", "Page Up").
	FString KeyLabel(FName Id) const;
	static FString LabelOf(const FKey& Key);
	// Gives the key to this; whatever had it before takes this one's old key, so no key does two things.
	void SetKey(FName Id, const FKey& NewKey);
	void ResetKeys();
	// A key the player may choose: a keyboard key that the game does not keep for itself (Enter, Esc, the arrows).
	static bool CanBind(const FKey& Key);

	static FPortsSettings& Get();
	void Save() const;

	double MoveFactor() const { return MoveSpeed <= 0 ? 0.55 : MoveSpeed == 1 ? 1.0 : 1.8; }
	double ZoomFactor() const { return ZoomSpeed <= 0 ? 0.5 : ZoomSpeed == 1 ? 1.0 : 1.8; }

private:
	void Load();
	TMap<FName, FKey> Keys;
};
