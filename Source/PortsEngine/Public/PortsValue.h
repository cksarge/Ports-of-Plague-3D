// A JSON value that keeps the order of its keys, like a JavaScript object.
// The web version's game state is one plain JSON object; the parts of it whose
// shape varies (log entries, waiting decisions, card data) are kept in this
// form so they stay identical, key for key, to what the web version produces.
#pragma once

#include "CoreMinimal.h"
#include <initializer_list>

struct FPortsEntry;

class PORTSENGINE_API FPortsValue
{
public:
	enum class EType : uint8 { Undefined, Null, Bool, Number, String, Array, Object };

	FPortsValue() = default;
	FPortsValue(bool bValue) : Type(EType::Bool), Flag(bValue) {}
	FPortsValue(int32 Value) : Type(EType::Number), Number(Value) {}
	FPortsValue(uint32 Value) : Type(EType::Number), Number(Value) {}
	FPortsValue(int64 Value) : Type(EType::Number), Number(static_cast<double>(Value)) {}
	FPortsValue(double Value) : Type(EType::Number), Number(Value) {}
	FPortsValue(const FString& Value) : Type(EType::String), Text(Value) {}
	FPortsValue(const TCHAR* Value) : Type(EType::String), Text(Value) {}
	// Any other pointer would silently turn into a bool.
	template <typename T> FPortsValue(T*) = delete;

	static FPortsValue Null() { FPortsValue V; V.Type = EType::Null; return V; }
	static FPortsValue Array() { FPortsValue V; V.Type = EType::Array; return V; }
	static FPortsValue Object() { FPortsValue V; V.Type = EType::Object; return V; }
	static FPortsValue Array(std::initializer_list<FPortsValue> Items);
	static FPortsValue Object(std::initializer_list<FPortsEntry> Entries);
	static FPortsValue Strings(const TArray<FString>& Items);
	static FPortsValue Ints(const TArray<int32>& Items);

	EType GetType() const { return Type; }
	bool IsUndefined() const { return Type == EType::Undefined; }
	bool IsNull() const { return Type == EType::Null; }
	// Null or undefined: what JavaScript's ?? operator treats as missing.
	bool IsMissing() const { return Type == EType::Undefined || Type == EType::Null; }
	bool IsBool() const { return Type == EType::Bool; }
	bool IsNumber() const { return Type == EType::Number; }
	bool IsString() const { return Type == EType::String; }
	bool IsArray() const { return Type == EType::Array; }
	bool IsObject() const { return Type == EType::Object; }

	// JavaScript truthiness.
	bool Truthy() const;
	bool AsBool() const { return Type == EType::Bool && Flag; }
	double AsNumber(double Default = 0) const { return Type == EType::Number ? Number : Default; }
	int32 AsInt(int32 Default = 0) const { return Type == EType::Number ? static_cast<int32>(Number) : Default; }
	const FString& AsString() const;

	// Arrays.
	int32 Num() const { return Type == EType::Array ? Items.Num() : Type == EType::Object ? Keys.Num() : 0; }
	const FPortsValue& operator[](int32 Index) const;
	FPortsValue& At(int32 Index) { return Items[Index]; }
	FPortsValue& Add(const FPortsValue& Item);
	void Insert(int32 Index, const FPortsValue& Item) { Items.Insert(Item, Index); }
	void RemoveAt(int32 Index) { Items.RemoveAt(Index); }
	const TArray<FPortsValue>& GetItems() const { return Items; }
	TArray<FString> ToStrings() const;
	TArray<int32> ToInts() const;
	// True if the array holds this string.
	bool ContainsString(const FString& Value) const;

	// Objects. Get returns an undefined value for a missing key.
	bool Has(const TCHAR* Key) const;
	const FPortsValue& Get(const TCHAR* Key) const;
	const FPortsValue& Get(const FString& Key) const { return Get(*Key); }
	FPortsValue* Find(const TCHAR* Key);
	// Sets a key: in place if it exists, otherwise at the end (as JavaScript does).
	FPortsValue& Set(const TCHAR* Key, const FPortsValue& Value);
	FPortsValue& Set(const FString& Key, const FPortsValue& Value) { return Set(*Key, Value); }
	void Remove(const TCHAR* Key);
	const TArray<FString>& GetKeys() const { return Keys; }
	const FPortsValue& ValueAt(int32 Index) const { return Items[Index]; }

	// Text exactly as JavaScript's JSON.stringify writes it.
	FString ToJson() const;
	void AppendJson(FString& Out) const;
	static bool Parse(const FString& Json, FPortsValue& Out);

	bool operator==(const FPortsValue& Other) const;
	bool operator!=(const FPortsValue& Other) const { return !(*this == Other); }

private:
	EType Type = EType::Undefined;
	bool Flag = false;
	double Number = 0;
	FString Text;
	// Array items, or an object's values in the order of Keys.
	TArray<FPortsValue> Items;
	TArray<FString> Keys;
};

// One key and its value, for writing objects out in code: { TEXT("type"), TEXT("ship") }.
struct FPortsEntry
{
	const TCHAR* Key;
	FPortsValue Value;
};
